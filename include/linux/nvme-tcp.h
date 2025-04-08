/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NVMe over Fabrics TCP protocol header.
 * Copyright (c) 2018 Lightbits Labs. All rights reserved.
 */

#ifndef _LINUX_NVME_TCP_H
#define _LINUX_NVME_TCP_H

#include <linux/nvme.h>

#define NVME_TCP_DISC_PORT	8009
#define NVME_TCP_ADMIN_CCSZ	SZ_8K
#define NVME_TCP_DIGEST_LENGTH	4
#define NVME_TCP_MIN_MAXH2CDATA 4096
#define NVME_TCP_MIN_C2HTERM_PLEN	24
#define NVME_TCP_MAX_C2HTERM_PLEN	152

enum nvme_tcp_pfv {
	NVME_TCP_PFV_1_0 = 0x0,
};

enum nvme_tcp_tls_cipher {
	NVME_TCP_TLS_CIPHER_INVALID     = 0,
	NVME_TCP_TLS_CIPHER_SHA256      = 1,
	NVME_TCP_TLS_CIPHER_SHA384      = 2,
};

enum nvme_tcp_fatal_error_status {
	NVME_TCP_FES_INVALID_PDU_HDR		= 0x01,
	NVME_TCP_FES_PDU_SEQ_ERR		= 0x02,
	NVME_TCP_FES_HDR_DIGEST_ERR		= 0x03,
	NVME_TCP_FES_DATA_OUT_OF_RANGE		= 0x04,
	NVME_TCP_FES_R2T_LIMIT_EXCEEDED		= 0x05,
	NVME_TCP_FES_DATA_LIMIT_EXCEEDED	= 0x05,
	NVME_TCP_FES_UNSUPPORTED_PARAM		= 0x06,
};

enum nvme_tcp_digest_option {
	NVME_TCP_HDR_DIGEST_ENABLE	= (1 << 0),
	NVME_TCP_DATA_DIGEST_ENABLE	= (1 << 1),
};

enum nvme_tcp_pdu_type {
	nvme_tcp_icreq		= 0x0,
	nvme_tcp_icresp		= 0x1,
	nvme_tcp_h2c_term	= 0x2,
	nvme_tcp_c2h_term	= 0x3,
	nvme_tcp_cmd		= 0x4,
	nvme_tcp_rsp		= 0x5,
	nvme_tcp_h2c_data	= 0x6,
	nvme_tcp_c2h_data	= 0x7,
	nvme_tcp_r2t		= 0x9,
};

enum nvme_tcp_pdu_flags {
	NVME_TCP_F_HDGST		= (1 << 0),
	NVME_TCP_F_DDGST		= (1 << 1),
	NVME_TCP_F_DATA_LAST		= (1 << 2),
	NVME_TCP_F_DATA_SUCCESS		= (1 << 3),
};

/**
 * struct nvme_tcp_hdr - nvme tcp pdu common header
 *
 * @type:          pdu type
 * @flags:         pdu specific flags
 * @hlen:          pdu header length
 * @pdo:           pdu data offset
 * @plen:          pdu wire byte length
 */
struct nvme_tcp_hdr {
	__u8	type;
	__u8	flags;
	__u8	hlen;
	__u8	pdo;
	__le32	plen;
};

/**
 * struct nvme_tcp_icreq_pdu - nvme tcp initialize connection request pdu
 *
 * @hdr:           pdu generic header
 * @pfv:           pdu version format
 * @hpda:          host pdu data alignment (dwords, 0's based)
 * @digest:        digest types enabled
 * @maxr2t:        maximum r2ts per request supported
 */
struct nvme_tcp_icreq_pdu {
	struct nvme_tcp_hdr	hdr;
	__le16			pfv;
	__u8			hpda;
	__u8			digest;
	__le32			maxr2t;
	__u8			rsvd2[112];
};

/**
 * struct nvme_tcp_icresp_pdu - nvme tcp initialize connection response pdu
 *
 * @hdr:           pdu common header
 * @pfv:           pdu version format
 * @cpda:          controller pdu data alignment (dowrds, 0's based)
 * @digest:        digest types enabled
 * @maxdata:       maximum data capsules per r2t supported
 */
struct nvme_tcp_icresp_pdu {
	struct nvme_tcp_hdr	hdr;
	__le16			pfv;
	__u8			cpda;
	__u8			digest;
	__le32			maxdata;
	__u8			rsvd[112];
};

/**
 * struct nvme_tcp_term_pdu - nvme tcp terminate connection pdu
 *
 * @hdr:           pdu common header
 * @fes:           fatal error status
 * @fei:           fatal error information
 */
struct nvme_tcp_term_pdu {
	struct nvme_tcp_hdr	hdr;
	__le16			fes;
	__le16			feil;
	__le16			feiu;
	__u8			rsvd[10];
};

/**
 * struct nvme_tcp_cmd_pdu - nvme tcp command capsule pdu
 *
 * @hdr:           pdu common header
 * @cmd:           nvme command
 */
struct nvme_tcp_cmd_pdu {
	struct nvme_tcp_hdr	hdr;
	struct nvme_command	cmd;
};

/**
 * struct nvme_tcp_rsp_pdu - nvme tcp response capsule pdu
 *
 * @hdr:           pdu common header
 * @hdr:           nvme-tcp generic header
 * @cqe:           nvme completion queue entry
 */
struct nvme_tcp_rsp_pdu {
	struct nvme_tcp_hdr	hdr;
	struct nvme_completion	cqe;
};

/**
 * struct nvme_tcp_r2t_pdu - nvme tcp ready-to-transfer pdu
 *
 * @hdr:           pdu common header
 * @command_id:    nvme command identifier which this relates to
 * @ttag:          transfer tag (controller generated)
 * @r2t_offset:    offset from the start of the command data
 * @r2t_length:    length the host is allowed to send
 */
struct nvme_tcp_r2t_pdu {
	struct nvme_tcp_hdr	hdr;
	__u16			command_id;
	__u16			ttag;
	__le32			r2t_offset;
	__le32			r2t_length;
	__u8			rsvd[4];
};

/**
 * struct nvme_tcp_data_pdu - nvme tcp data pdu
 *
 * @hdr:           pdu common header
 * @command_id:    nvme command identifier which this relates to
 * @ttag:          transfer tag (controller generated)
 * @data_offset:   offset from the start of the command data
 * @data_length:   length of the data stream
 */
struct nvme_tcp_data_pdu {
	struct nvme_tcp_hdr	hdr;
	__u16			command_id;
	__u16			ttag;
	__le32			data_offset;
	__le32			data_length;
	__u8			rsvd[4];
};

union nvme_tcp_pdu {
	struct nvme_tcp_icreq_pdu	icreq;
	struct nvme_tcp_icresp_pdu	icresp;
	struct nvme_tcp_cmd_pdu		cmd;
	struct nvme_tcp_rsp_pdu		rsp;
	struct nvme_tcp_r2t_pdu		r2t;
	struct nvme_tcp_data_pdu	data;
};

#endif /* _LINUX_NVME_TCP_H */
