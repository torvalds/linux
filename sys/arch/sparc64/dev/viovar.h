/*	$OpenBSD: viovar.h,v 1.2 2009/01/12 19:52:39 kettenis Exp $	*/
/*
 * Copyright (c) 2009 Mark Kettenis
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
 */

/*
 * Virtual IO device protocol.
 */

struct vio_msg_tag {
	uint8_t		type;
	uint8_t		stype;
	uint16_t	stype_env;
	uint32_t	sid;
};

struct vio_msg {
	uint64_t 	hdr;
	uint8_t		type;
	uint8_t		stype;
	uint16_t	stype_env;
	uint32_t	sid;
	uint16_t	major;
	uint16_t	minor;
	uint8_t		dev_class;
};

/* Message types. */
#define VIO_TYPE_CTRL		0x01
#define VIO_TYPE_DATA		0x02
#define VIO_TYPE_ERR		0x04

/* Sub-Message types. */
#define VIO_SUBTYPE_INFO	0x01
#define VIO_SUBTYPE_ACK		0x02
#define VIO_SUBTYPE_NACK	0x04

/* Sub-Type envelopes. */
#define VIO_VER_INFO		0x0001
#define VIO_ATTR_INFO		0x0002
#define VIO_DRING_REG		0x0003
#define VIO_DRING_UNREG		0x0004
#define VIO_RDX			0x0005

#define VIO_PKT_DATA		0x0040
#define VIO_DESC_DATA		0x0041
#define VIO_DRING_DATA		0x0042

struct vio_ver_info {
	struct vio_msg_tag	tag;
	uint16_t		major;
	uint16_t		minor;
	uint8_t			dev_class;
	uint8_t			_reserved1[3];
	uint64_t		_reserved2[5];
};

/* Device types. */
#define VDEV_NETWORK		0x01
#define VDEV_NETWORK_SWITCH	0x02
#define VDEV_DISK		0x03
#define VDEV_DISK_SERVER	0x04

struct vio_dring_reg {
	struct vio_msg_tag	tag;
	uint64_t		dring_ident;
	uint32_t		num_descriptors;
	uint32_t		descriptor_size;
	uint16_t		options;
	uint16_t		_reserved;
	uint32_t		ncookies;
	struct ldc_cookie	cookie[1];
};

/* Ring options. */
#define VIO_TX_RING		0x0001
#define VIO_RX_RING		0x0002

/* Transfer modes. */
#define VIO_PKT_MODE		0x01
#define VIO_DESC_MODE		0x02
#define VIO_DRING_MODE		0x03

struct vio_dring_hdr {
	uint8_t		dstate;
	uint8_t		ack: 1;
	uint16_t	_reserved[3];
};

/* Descriptor states. */
#define VIO_DESC_FREE		0x01
#define VIO_DESC_READY		0x02
#define VIO_DESC_ACCEPTED	0x03
#define VIO_DESC_DONE		0x04

struct vio_dring_msg {
	struct vio_msg_tag	tag;
	uint64_t		seq_no;
	uint64_t		dring_ident;
	uint32_t		start_idx;
	uint32_t		end_idx;
	uint8_t			proc_state;
	uint8_t			_reserved1[7];
	uint64_t		_reserved2[2];
};

/* Ring states. */
#define VIO_DP_ACTIVE	0x01
#define VIO_DP_STOPPED	0x02

struct vio_rdx {
	struct vio_msg_tag	tag;
	uint64_t		_reserved[6];
};

