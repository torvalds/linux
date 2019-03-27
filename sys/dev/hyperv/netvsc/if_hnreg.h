/*-
 * Copyright (c) 2016-2017 Microsoft Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _IF_HNREG_H_
#define _IF_HNREG_H_

#include <sys/param.h>
#include <sys/systm.h>

/*
 * NDIS protocol version numbers
 */
#define HN_NDIS_VERSION_6_1		0x00060001
#define HN_NDIS_VERSION_6_20		0x00060014
#define HN_NDIS_VERSION_6_30		0x0006001e
#define HN_NDIS_VERSION_MAJOR(ver)	(((ver) & 0xffff0000) >> 16)
#define HN_NDIS_VERSION_MINOR(ver)	((ver) & 0xffff)

/*
 * NVS versions.
 */
#define HN_NVS_VERSION_1		0x00002
#define HN_NVS_VERSION_2		0x30002
#define HN_NVS_VERSION_4		0x40000
#define HN_NVS_VERSION_5		0x50000

#define HN_NVS_RXBUF_SIG		0xcafe
#define HN_NVS_CHIM_SIG			0xface

#define HN_NVS_CHIM_IDX_INVALID		0xffffffff

#define HN_NVS_RNDIS_MTYPE_DATA		0
#define HN_NVS_RNDIS_MTYPE_CTRL		1

/*
 * NVS message transacion status codes.
 */
#define HN_NVS_STATUS_OK		1
#define HN_NVS_STATUS_FAILED		2

/*
 * NVS request/response message types.
 */
#define HN_NVS_TYPE_INIT		1
#define HN_NVS_TYPE_INIT_RESP		2
#define HN_NVS_TYPE_NDIS_INIT		100
#define HN_NVS_TYPE_RXBUF_CONN		101
#define HN_NVS_TYPE_RXBUF_CONNRESP	102
#define HN_NVS_TYPE_RXBUF_DISCONN	103
#define HN_NVS_TYPE_CHIM_CONN		104
#define HN_NVS_TYPE_CHIM_CONNRESP	105
#define HN_NVS_TYPE_CHIM_DISCONN	106
#define HN_NVS_TYPE_RNDIS		107
#define HN_NVS_TYPE_RNDIS_ACK		108
#define HN_NVS_TYPE_NDIS_CONF		125
#define HN_NVS_TYPE_VFASSOC_NOTE	128	/* notification */
#define HN_NVS_TYPE_SET_DATAPATH	129
#define HN_NVS_TYPE_SUBCH_REQ		133
#define HN_NVS_TYPE_SUBCH_RESP		133	/* same as SUBCH_REQ */
#define HN_NVS_TYPE_TXTBL_NOTE		134	/* notification */

/*
 * Any size less than this one will _not_ work, e.g. hn_nvs_init
 * only has 12B valid data, however, if only 12B data were sent,
 * Hypervisor would never reply.
 */
#define HN_NVS_REQSIZE_MIN		32

/* NVS message common header */
struct hn_nvs_hdr {
	uint32_t	nvs_type;
} __packed;

struct hn_nvs_init {
	uint32_t	nvs_type;	/* HN_NVS_TYPE_INIT */
	uint32_t	nvs_ver_min;
	uint32_t	nvs_ver_max;
	uint8_t		nvs_rsvd[20];
} __packed;
CTASSERT(sizeof(struct hn_nvs_init) >= HN_NVS_REQSIZE_MIN);

struct hn_nvs_init_resp {
	uint32_t	nvs_type;	/* HN_NVS_TYPE_INIT_RESP */
	uint32_t	nvs_ver;	/* deprecated */
	uint32_t	nvs_rsvd;
	uint32_t	nvs_status;	/* HN_NVS_STATUS_ */
} __packed;

/* No reponse */
struct hn_nvs_ndis_conf {
	uint32_t	nvs_type;	/* HN_NVS_TYPE_NDIS_CONF */
	uint32_t	nvs_mtu;
	uint32_t	nvs_rsvd;
	uint64_t	nvs_caps;	/* HN_NVS_NDIS_CONF_ */
	uint8_t		nvs_rsvd1[12];
} __packed;
CTASSERT(sizeof(struct hn_nvs_ndis_conf) >= HN_NVS_REQSIZE_MIN);

#define HN_NVS_NDIS_CONF_SRIOV		0x0004
#define HN_NVS_NDIS_CONF_VLAN		0x0008

/* No response */
struct hn_nvs_ndis_init {
	uint32_t	nvs_type;	/* HN_NVS_TYPE_NDIS_INIT */
	uint32_t	nvs_ndis_major;	/* NDIS_VERSION_MAJOR_ */
	uint32_t	nvs_ndis_minor;	/* NDIS_VERSION_MINOR_ */
	uint8_t		nvs_rsvd[20];
} __packed;
CTASSERT(sizeof(struct hn_nvs_ndis_init) >= HN_NVS_REQSIZE_MIN);

#define HN_NVS_DATAPATH_SYNTH		0
#define HN_NVS_DATAPATH_VF		1

/* No response */
struct hn_nvs_datapath {
	uint32_t	nvs_type;	/* HN_NVS_TYPE_SET_DATAPATH */
	uint32_t	nvs_active_path;/* HN_NVS_DATAPATH_* */
	uint32_t	nvs_rsvd[6];
} __packed;
CTASSERT(sizeof(struct hn_nvs_datapath) >= HN_NVS_REQSIZE_MIN);

struct hn_nvs_rxbuf_conn {
	uint32_t	nvs_type;	/* HN_NVS_TYPE_RXBUF_CONN */
	uint32_t	nvs_gpadl;	/* RXBUF vmbus GPADL */
	uint16_t	nvs_sig;	/* HN_NVS_RXBUF_SIG */
	uint8_t		nvs_rsvd[22];
} __packed;
CTASSERT(sizeof(struct hn_nvs_rxbuf_conn) >= HN_NVS_REQSIZE_MIN);

struct hn_nvs_rxbuf_sect {
	uint32_t	nvs_start;
	uint32_t	nvs_slotsz;
	uint32_t	nvs_slotcnt;
	uint32_t	nvs_end;
} __packed;

struct hn_nvs_rxbuf_connresp {
	uint32_t	nvs_type;	/* HN_NVS_TYPE_RXBUF_CONNRESP */
	uint32_t	nvs_status;	/* HN_NVS_STATUS_ */
	uint32_t	nvs_nsect;	/* # of elem in nvs_sect */
	struct hn_nvs_rxbuf_sect nvs_sect[];
} __packed;

/* No response */
struct hn_nvs_rxbuf_disconn {
	uint32_t	nvs_type;	/* HN_NVS_TYPE_RXBUF_DISCONN */
	uint16_t	nvs_sig;	/* HN_NVS_RXBUF_SIG */
	uint8_t		nvs_rsvd[26];
} __packed;
CTASSERT(sizeof(struct hn_nvs_rxbuf_disconn) >= HN_NVS_REQSIZE_MIN);

struct hn_nvs_chim_conn {
	uint32_t	nvs_type;	/* HN_NVS_TYPE_CHIM_CONN */
	uint32_t	nvs_gpadl;	/* chimney buf vmbus GPADL */
	uint16_t	nvs_sig;	/* NDIS_NVS_CHIM_SIG */
	uint8_t		nvs_rsvd[22];
} __packed;
CTASSERT(sizeof(struct hn_nvs_chim_conn) >= HN_NVS_REQSIZE_MIN);

struct hn_nvs_chim_connresp {
	uint32_t	nvs_type;	/* HN_NVS_TYPE_CHIM_CONNRESP */
	uint32_t	nvs_status;	/* HN_NVS_STATUS_ */
	uint32_t	nvs_sectsz;	/* section size */
} __packed;

/* No response */
struct hn_nvs_chim_disconn {
	uint32_t	nvs_type;	/* HN_NVS_TYPE_CHIM_DISCONN */
	uint16_t	nvs_sig;	/* HN_NVS_CHIM_SIG */
	uint8_t		nvs_rsvd[26];
} __packed;
CTASSERT(sizeof(struct hn_nvs_chim_disconn) >= HN_NVS_REQSIZE_MIN);

#define HN_NVS_SUBCH_OP_ALLOC		1

struct hn_nvs_subch_req {
	uint32_t	nvs_type;	/* HN_NVS_TYPE_SUBCH_REQ */
	uint32_t	nvs_op;		/* HN_NVS_SUBCH_OP_ */
	uint32_t	nvs_nsubch;
	uint8_t		nvs_rsvd[20];
} __packed;
CTASSERT(sizeof(struct hn_nvs_subch_req) >= HN_NVS_REQSIZE_MIN);

struct hn_nvs_subch_resp {
	uint32_t	nvs_type;	/* HN_NVS_TYPE_SUBCH_RESP */
	uint32_t	nvs_status;	/* HN_NVS_STATUS_ */
	uint32_t	nvs_nsubch;
} __packed;

struct hn_nvs_rndis {
	uint32_t	nvs_type;	/* HN_NVS_TYPE_RNDIS */
	uint32_t	nvs_rndis_mtype;/* HN_NVS_RNDIS_MTYPE_ */
	/*
	 * Chimney sending buffer index and size.
	 *
	 * NOTE:
	 * If nvs_chim_idx is set to HN_NVS_CHIM_IDX_INVALID
	 * and nvs_chim_sz is set to 0, then chimney sending
	 * buffer is _not_ used by this RNDIS message.
	 */
	uint32_t	nvs_chim_idx;
	uint32_t	nvs_chim_sz;
	uint8_t		nvs_rsvd[16];
} __packed;
CTASSERT(sizeof(struct hn_nvs_rndis) >= HN_NVS_REQSIZE_MIN);

struct hn_nvs_rndis_ack {
	uint32_t	nvs_type;	/* HN_NVS_TYPE_RNDIS_ACK */
	uint32_t	nvs_status;	/* HN_NVS_STATUS_ */
	uint8_t		nvs_rsvd[24];
} __packed;
CTASSERT(sizeof(struct hn_nvs_rndis_ack) >= HN_NVS_REQSIZE_MIN);

/*
 * RNDIS extension
 */

/* Per-packet hash info */
#define HN_NDIS_HASH_INFO_SIZE		sizeof(uint32_t)
#define HN_NDIS_PKTINFO_TYPE_HASHINF	NDIS_PKTINFO_TYPE_ORIG_NBLIST
/* NDIS_HASH_ */

/* Per-packet hash value */
#define HN_NDIS_HASH_VALUE_SIZE		sizeof(uint32_t)
#define HN_NDIS_PKTINFO_TYPE_HASHVAL	NDIS_PKTINFO_TYPE_PKT_CANCELID

/* Per-packet-info size */
#define HN_RNDIS_PKTINFO_SIZE(dlen)	\
	__offsetof(struct rndis_pktinfo, rm_data[dlen])

#endif	/* !_IF_HNREG_H_ */
