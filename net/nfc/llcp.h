/*
 * Copyright (C) 2011  Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

enum llcp_state {
	LLCP_CONNECTED = 1, /* wait_for_packet() wants that */
	LLCP_CONNECTING,
	LLCP_CLOSED,
	LLCP_BOUND,
	LLCP_LISTEN,
};

#define LLCP_DEFAULT_LTO 100
#define LLCP_DEFAULT_RW  1
#define LLCP_DEFAULT_MIU 128

#define LLCP_MAX_LTO  0xff
#define LLCP_MAX_RW   15
#define LLCP_MAX_MIUX 0x7ff
#define LLCP_MAX_MIU (LLCP_MAX_MIUX + 128)

#define LLCP_WKS_NUM_SAP   16
#define LLCP_SDP_NUM_SAP   16
#define LLCP_LOCAL_NUM_SAP 32
#define LLCP_LOCAL_SAP_OFFSET (LLCP_WKS_NUM_SAP + LLCP_SDP_NUM_SAP)
#define LLCP_MAX_SAP (LLCP_WKS_NUM_SAP + LLCP_SDP_NUM_SAP + LLCP_LOCAL_NUM_SAP)
#define LLCP_SDP_UNBOUND   (LLCP_MAX_SAP + 1)

struct nfc_llcp_sock;

struct llcp_sock_list {
	struct hlist_head head;
	rwlock_t          lock;
};

struct nfc_llcp_sdp_tlv {
	u8 *tlv;
	u8 tlv_len;

	char *uri;
	u8 tid;
	u8 sap;

	unsigned long time;

	struct hlist_node node;
};

struct nfc_llcp_local {
	struct list_head list;
	struct nfc_dev *dev;

	struct kref ref;

	struct mutex sdp_lock;

	struct timer_list link_timer;
	struct sk_buff_head tx_queue;
	struct work_struct	 tx_work;
	struct work_struct	 rx_work;
	struct sk_buff *rx_pending;
	struct work_struct	 timeout_work;

	u32 target_idx;
	u8 rf_mode;
	u8 comm_mode;
	u8 lto;
	u8 rw;
	__be16 miux;
	unsigned long local_wks;      /* Well known services */
	unsigned long local_sdp;      /* Local services  */
	unsigned long local_sap; /* Local SAPs, not available for discovery */
	atomic_t local_sdp_cnt[LLCP_SDP_NUM_SAP];

	/* local */
	u8 gb[NFC_MAX_GT_LEN];
	u8 gb_len;

	/* remote */
	u8 remote_gb[NFC_MAX_GT_LEN];
	u8 remote_gb_len;

	u8  remote_version;
	u16 remote_miu;
	u16 remote_lto;
	u8  remote_opt;
	u16 remote_wks;

	struct mutex sdreq_lock;
	struct hlist_head pending_sdreqs;
	struct timer_list sdreq_timer;
	struct work_struct sdreq_timeout_work;
	u8 sdreq_next_tid;

	/* sockets array */
	struct llcp_sock_list sockets;
	struct llcp_sock_list connecting_sockets;
	struct llcp_sock_list raw_sockets;
};

struct nfc_llcp_sock {
	struct sock sk;
	struct nfc_dev *dev;
	struct nfc_llcp_local *local;
	u32 target_idx;
	u32 nfc_protocol;

	/* Link parameters */
	u8 ssap;
	u8 dsap;
	char *service_name;
	size_t service_name_len;
	u8 rw;
	__be16 miux;


	/* Remote link parameters */
	u8 remote_rw;
	u16 remote_miu;

	/* Link variables */
	u8 send_n;
	u8 send_ack_n;
	u8 recv_n;
	u8 recv_ack_n;

	/* Is the remote peer ready to receive */
	u8 remote_ready;

	/* Reserved source SAP */
	u8 reserved_ssap;

	struct sk_buff_head tx_queue;
	struct sk_buff_head tx_pending_queue;

	struct list_head accept_queue;
	struct sock *parent;
};

struct nfc_llcp_ui_cb {
	__u8 dsap;
	__u8 ssap;
};

#define nfc_llcp_ui_skb_cb(__skb) ((struct nfc_llcp_ui_cb *)&((__skb)->cb[0]))

#define nfc_llcp_sock(sk) ((struct nfc_llcp_sock *) (sk))
#define nfc_llcp_dev(sk)  (nfc_llcp_sock((sk))->dev)

#define LLCP_HEADER_SIZE   2
#define LLCP_SEQUENCE_SIZE 1
#define LLCP_AGF_PDU_HEADER_SIZE 2

/* LLCP versions: 1.1 is 1.0 plus SDP */
#define LLCP_VERSION_10 0x10
#define LLCP_VERSION_11 0x11

/* LLCP PDU types */
#define LLCP_PDU_SYMM     0x0
#define LLCP_PDU_PAX      0x1
#define LLCP_PDU_AGF      0x2
#define LLCP_PDU_UI       0x3
#define LLCP_PDU_CONNECT  0x4
#define LLCP_PDU_DISC     0x5
#define LLCP_PDU_CC       0x6
#define LLCP_PDU_DM       0x7
#define LLCP_PDU_FRMR     0x8
#define LLCP_PDU_SNL      0x9
#define LLCP_PDU_I        0xc
#define LLCP_PDU_RR       0xd
#define LLCP_PDU_RNR      0xe

/* Parameters TLV types */
#define LLCP_TLV_VERSION 0x1
#define LLCP_TLV_MIUX    0x2
#define LLCP_TLV_WKS     0x3
#define LLCP_TLV_LTO     0x4
#define LLCP_TLV_RW      0x5
#define LLCP_TLV_SN      0x6
#define LLCP_TLV_OPT     0x7
#define LLCP_TLV_SDREQ   0x8
#define LLCP_TLV_SDRES   0x9
#define LLCP_TLV_MAX     0xa

/* Well known LLCP SAP */
#define LLCP_SAP_SDP   0x1
#define LLCP_SAP_IP    0x2
#define LLCP_SAP_OBEX  0x3
#define LLCP_SAP_SNEP  0x4
#define LLCP_SAP_MAX   0xff

/* Disconnection reason code */
#define LLCP_DM_DISC    0x00
#define LLCP_DM_NOCONN  0x01
#define LLCP_DM_NOBOUND 0x02
#define LLCP_DM_REJ     0x03


void nfc_llcp_sock_link(struct llcp_sock_list *l, struct sock *s);
void nfc_llcp_sock_unlink(struct llcp_sock_list *l, struct sock *s);
void nfc_llcp_socket_remote_param_init(struct nfc_llcp_sock *sock);
struct nfc_llcp_local *nfc_llcp_find_local(struct nfc_dev *dev);
struct nfc_llcp_local *nfc_llcp_local_get(struct nfc_llcp_local *local);
int nfc_llcp_local_put(struct nfc_llcp_local *local);
u8 nfc_llcp_get_sdp_ssap(struct nfc_llcp_local *local,
			 struct nfc_llcp_sock *sock);
u8 nfc_llcp_get_local_ssap(struct nfc_llcp_local *local);
void nfc_llcp_put_ssap(struct nfc_llcp_local *local, u8 ssap);
int nfc_llcp_queue_i_frames(struct nfc_llcp_sock *sock);
void nfc_llcp_send_to_raw_sock(struct nfc_llcp_local *local,
			       struct sk_buff *skb, u8 direction);

/* Sock API */
struct sock *nfc_llcp_sock_alloc(struct socket *sock, int type, gfp_t gfp);
void nfc_llcp_sock_free(struct nfc_llcp_sock *sock);
void nfc_llcp_accept_unlink(struct sock *sk);
void nfc_llcp_accept_enqueue(struct sock *parent, struct sock *sk);
struct sock *nfc_llcp_accept_dequeue(struct sock *sk, struct socket *newsock);

/* TLV API */
int nfc_llcp_parse_gb_tlv(struct nfc_llcp_local *local,
			  u8 *tlv_array, u16 tlv_array_len);
int nfc_llcp_parse_connection_tlv(struct nfc_llcp_sock *sock,
				  u8 *tlv_array, u16 tlv_array_len);

/* Commands API */
void nfc_llcp_recv(void *data, struct sk_buff *skb, int err);
u8 *nfc_llcp_build_tlv(u8 type, u8 *value, u8 value_length, u8 *tlv_length);
struct nfc_llcp_sdp_tlv *nfc_llcp_build_sdres_tlv(u8 tid, u8 sap);
struct nfc_llcp_sdp_tlv *nfc_llcp_build_sdreq_tlv(u8 tid, char *uri,
						  size_t uri_len);
void nfc_llcp_free_sdp_tlv(struct nfc_llcp_sdp_tlv *sdp);
void nfc_llcp_free_sdp_tlv_list(struct hlist_head *sdp_head);
void nfc_llcp_recv(void *data, struct sk_buff *skb, int err);
int nfc_llcp_disconnect(struct nfc_llcp_sock *sock);
int nfc_llcp_send_symm(struct nfc_dev *dev);
int nfc_llcp_send_connect(struct nfc_llcp_sock *sock);
int nfc_llcp_send_cc(struct nfc_llcp_sock *sock);
int nfc_llcp_send_snl_sdres(struct nfc_llcp_local *local,
			    struct hlist_head *tlv_list, size_t tlvs_len);
int nfc_llcp_send_snl_sdreq(struct nfc_llcp_local *local,
			    struct hlist_head *tlv_list, size_t tlvs_len);
int nfc_llcp_send_dm(struct nfc_llcp_local *local, u8 ssap, u8 dsap, u8 reason);
int nfc_llcp_send_disconnect(struct nfc_llcp_sock *sock);
int nfc_llcp_send_i_frame(struct nfc_llcp_sock *sock,
			  struct msghdr *msg, size_t len);
int nfc_llcp_send_ui_frame(struct nfc_llcp_sock *sock, u8 ssap, u8 dsap,
			   struct msghdr *msg, size_t len);
int nfc_llcp_send_rr(struct nfc_llcp_sock *sock);

/* Socket API */
int __init nfc_llcp_sock_init(void);
void nfc_llcp_sock_exit(void);
