/*	$OpenBSD: ds.h,v 1.4 2018/07/13 08:46:07 kettenis Exp $	*/

/*
 * Copyright (c) 2012 Mark Kettenis
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

#include <sys/types.h>
#include <sys/queue.h>

/*
 * LDC virtual link layer protocol.
 */

#define LDC_VERSION_MAJOR	1
#define LDC_VERSION_MINOR	0

#define LDC_PKT_PAYLOAD	48

struct ldc_pkt {
	uint8_t		type;
	uint8_t		stype;
	uint8_t		ctrl;
	uint8_t		env;
	uint32_t	seqid;

	uint16_t	major;
	uint16_t	minor;
	uint32_t	ackid;

	uint64_t	data[6];
};

/* Packet types. */
#define LDC_CTRL	0x01
#define LDC_DATA	0x02
#define LDC_ERR		0x10

/* Packet subtypes. */
#define LDC_INFO	0x01
#define LDC_ACK		0x02
#define LDC_NACK	0x04

/* Control info values. */
#define LDC_VERS	0x01
#define LDC_RTS		0x02
#define LDC_RTR		0x03
#define LDC_RDX		0x04

/* Packet envelope. */
#define LDC_MODE_RAW		0x00
#define LDC_MODE_UNRELIABLE	0x01
#define LDC_MODE_RELIABLE	0x03

#define LDC_LEN_MASK	0x3f
#define LDC_FRAG_MASK	0xc0
#define LDC_FRAG_START	0x40
#define LDC_FRAG_STOP	0x80

#define LDC_MSG_MAX	4096

struct ldc_conn {
	int		lc_fd;

	uint32_t	lc_tx_seqid;
	uint8_t		lc_state;
#define LDC_SND_VERS	1
#define LDC_RCV_VERS	2
#define LDC_SND_RTS	3
#define LDC_SND_RTR	4
#define LDC_SND_RDX	5

	uint64_t	lc_msg[LDC_MSG_MAX / 8];
	size_t		lc_len;

	void		*lc_cookie;
	void		(*lc_reset)(struct ldc_conn *);
	void		(*lc_start)(struct ldc_conn *);
	void		(*lc_rx_data)(struct ldc_conn *, void *, size_t);
};

void	ldc_rx_ctrl(struct ldc_conn *, struct ldc_pkt *);
void	ldc_rx_data(struct ldc_conn *, struct ldc_pkt *);

void	ldc_send_vers(struct ldc_conn *);

void	ldc_reset(struct ldc_conn *);

struct ds_msg {
	uint32_t	msg_type;
	uint32_t	payload_len;
	uint64_t	data[5];
};

struct ds_init_req {
	uint32_t	msg_type;
	uint32_t	payload_len;
	uint16_t	major_vers;
	uint16_t	minor_vers;
} __packed;

struct ds_init_ack {
	uint32_t	msg_type;
	uint32_t	payload_len;
	uint16_t	minor_vers;
} __packed;

#define DS_INIT_REQ	0x00
#define DS_INIT_ACK	0x01
#define DS_INIT_NACK	0x02

struct ds_reg_req {
	uint32_t	msg_type;
	uint32_t	payload_len;
	uint64_t	svc_handle;
	uint16_t	major_vers;
	uint16_t	minor_vers;
	char		svc_id[1];
} __packed;

#define DS_REG_REQ	0x03

struct ds_reg_ack {
	uint32_t	msg_type;
	uint32_t	payload_len;
	uint64_t	svc_handle;
	uint16_t	minor_vers;
	uint8_t		_reserved[6];
} __packed;

#define DS_REG_ACK	0x04

struct ds_reg_nack {
	uint32_t	msg_type;
	uint32_t	payload_len;
	uint64_t	svc_handle;
	uint64_t	result;
	uint16_t	major_vers;
	uint8_t		_reserved[6];
} __packed;

#define DS_REG_NACK	0x05

struct ds_unreg {
	uint32_t	msg_type;
	uint32_t	payload_len;
	uint64_t	svc_handle;
} __packed;

#define DS_UNREG	0x06
#define DS_UNREG_ACK	0x07
#define DS_UNREG_NACK	0x08

struct ds_data {
	uint32_t	msg_type;
	uint32_t	payload_len;
	uint64_t	svc_handle;
	uint64_t	data[4];
};

#define DS_DATA		0x09

struct ds_nack {
	uint32_t	msg_type;
	uint32_t	payload_len;
	uint64_t	svc_handle;
	uint64_t	result;
} __packed;

#define DS_NACK		0x0a

#define DS_REG_VER_NACK	0x01
#define DS_REG_DUP	0x02
#define DS_INV_HDL	0x03
#define DS_TYPE_UNKNOWN	0x04

struct ds_service {
	const char	*ds_svc_id;
	uint16_t	ds_major_vers;
	uint16_t	ds_minor_vers;

	void		(*ds_start)(struct ldc_conn *, uint64_t);
	void		(*ds_rx_data)(struct ldc_conn *, uint64_t, void *,
			    size_t);
};

void	ldc_ack(struct ldc_conn *, uint32_t);
void	ds_rx_msg(struct ldc_conn *, void *, size_t);

void	ds_init_ack(struct ldc_conn *);
void	ds_reg_ack(struct ldc_conn *, uint64_t, uint16_t);
void	ds_reg_nack(struct ldc_conn *, uint64_t, uint16_t);
void	ds_unreg_ack(struct ldc_conn *, uint64_t);
void	ds_unreg_nack(struct ldc_conn *, uint64_t);

void	ds_receive_msg(struct ldc_conn *lc, void *, size_t);
void	ds_send_msg(struct ldc_conn *lc, void *, size_t);

struct ds_conn_svc {
	struct ds_service *service;
	uint64_t svc_handle;
	uint32_t ackid;

	TAILQ_ENTRY(ds_conn_svc) link;
};

struct ds_conn {
	char *path;
	void *cookie;
	int id;
	struct ldc_conn lc;
	int fd;

	TAILQ_HEAD(ds_conn_svc_head, ds_conn_svc) services;
	TAILQ_ENTRY(ds_conn) link;
};

struct ds_conn *ds_conn_open(const char *, void *);
void	ds_conn_register_service(struct ds_conn *, struct ds_service *);
void	ds_conn_serve(void);
void	ds_conn_handle(struct ds_conn *);
