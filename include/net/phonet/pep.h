/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * File: pep.h
 *
 * Phonet Pipe End Point sockets definitions
 *
 * Copyright (C) 2008 Nokia Corporation.
 */

#ifndef NET_PHONET_PEP_H
#define NET_PHONET_PEP_H

struct pep_sock {
	struct pn_sock		pn_sk;

	/* XXX: union-ify listening vs connected stuff ? */
	/* Listening socket stuff: */
	struct hlist_head	hlist;

	/* Connected socket stuff: */
	struct sock		*listener;
	struct sk_buff_head	ctrlreq_queue;
#define PNPIPE_CTRLREQ_MAX	10
	atomic_t		tx_credits;
	int			ifindex;
	u16			peer_type;	/* peer type/subtype */
	u8			pipe_handle;

	u8			rx_credits;
	u8			rx_fc;	/* RX flow control */
	u8			tx_fc;	/* TX flow control */
	u8			init_enable;	/* auto-enable at creation */
	u8			aligned;
};

static inline struct pep_sock *pep_sk(struct sock *sk)
{
	return (struct pep_sock *)sk;
}

extern const struct proto_ops phonet_stream_ops;

/* Pipe protocol definitions */
struct pnpipehdr {
	u8			utid; /* transaction ID */
	u8			message_id;
	u8			pipe_handle;
	union {
		u8		state_after_connect;	/* connect request */
		u8		state_after_reset;	/* reset request */
		u8		error_code;		/* any response */
		u8		pep_type;		/* status indication */
		u8		data0;			/* anything else */
	};
	u8			data[];
};
#define other_pep_type		data[0]

static inline struct pnpipehdr *pnp_hdr(struct sk_buff *skb)
{
	return (struct pnpipehdr *)skb_transport_header(skb);
}

#define MAX_PNPIPE_HEADER (MAX_PHONET_HEADER + 4)

enum {
	PNS_PIPE_CREATE_REQ = 0x00,
	PNS_PIPE_CREATE_RESP,
	PNS_PIPE_REMOVE_REQ,
	PNS_PIPE_REMOVE_RESP,

	PNS_PIPE_DATA = 0x20,
	PNS_PIPE_ALIGNED_DATA,

	PNS_PEP_CONNECT_REQ = 0x40,
	PNS_PEP_CONNECT_RESP,
	PNS_PEP_DISCONNECT_REQ,
	PNS_PEP_DISCONNECT_RESP,
	PNS_PEP_RESET_REQ,
	PNS_PEP_RESET_RESP,
	PNS_PEP_ENABLE_REQ,
	PNS_PEP_ENABLE_RESP,
	PNS_PEP_CTRL_REQ,
	PNS_PEP_CTRL_RESP,
	PNS_PEP_DISABLE_REQ = 0x4C,
	PNS_PEP_DISABLE_RESP,

	PNS_PEP_STATUS_IND = 0x60,
	PNS_PIPE_CREATED_IND,
	PNS_PIPE_RESET_IND = 0x63,
	PNS_PIPE_ENABLED_IND,
	PNS_PIPE_REDIRECTED_IND,
	PNS_PIPE_DISABLED_IND = 0x66,
};

#define PN_PIPE_INVALID_HANDLE	0xff
#define PN_PEP_TYPE_COMMON	0x00

/* Phonet pipe status indication */
enum {
	PN_PEP_IND_FLOW_CONTROL,
	PN_PEP_IND_ID_MCFC_GRANT_CREDITS,
};

/* Phonet pipe error codes */
enum {
	PN_PIPE_NO_ERROR,
	PN_PIPE_ERR_INVALID_PARAM,
	PN_PIPE_ERR_INVALID_HANDLE,
	PN_PIPE_ERR_INVALID_CTRL_ID,
	PN_PIPE_ERR_NOT_ALLOWED,
	PN_PIPE_ERR_PEP_IN_USE,
	PN_PIPE_ERR_OVERLOAD,
	PN_PIPE_ERR_DEV_DISCONNECTED,
	PN_PIPE_ERR_TIMEOUT,
	PN_PIPE_ERR_ALL_PIPES_IN_USE,
	PN_PIPE_ERR_GENERAL,
	PN_PIPE_ERR_NOT_SUPPORTED,
};

/* Phonet pipe states */
enum {
	PN_PIPE_DISABLE,
	PN_PIPE_ENABLE,
};

/* Phonet pipe sub-block types */
enum {
	PN_PIPE_SB_CREATE_REQ_PEP_SUB_TYPE,
	PN_PIPE_SB_CONNECT_REQ_PEP_SUB_TYPE,
	PN_PIPE_SB_REDIRECT_REQ_PEP_SUB_TYPE,
	PN_PIPE_SB_NEGOTIATED_FC,
	PN_PIPE_SB_REQUIRED_FC_TX,
	PN_PIPE_SB_PREFERRED_FC_RX,
	PN_PIPE_SB_ALIGNED_DATA,
};

/* Phonet pipe flow control models */
enum {
	PN_NO_FLOW_CONTROL,
	PN_LEGACY_FLOW_CONTROL,
	PN_ONE_CREDIT_FLOW_CONTROL,
	PN_MULTI_CREDIT_FLOW_CONTROL,
	PN_MAX_FLOW_CONTROL,
};

#define pn_flow_safe(fc) ((fc) >> 1)

/* Phonet pipe flow control states */
enum {
	PEP_IND_EMPTY,
	PEP_IND_BUSY,
	PEP_IND_READY,
};

#endif
