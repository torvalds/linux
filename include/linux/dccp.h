#ifndef _LINUX_DCCP_H
#define _LINUX_DCCP_H

#include <linux/types.h>
#include <asm/byteorder.h>

/**
 * struct dccp_hdr - generic part of DCCP packet header
 *
 * @dccph_sport - Relevant port on the endpoint that sent this packet
 * @dccph_dport - Relevant port on the other endpoint
 * @dccph_doff - Data Offset from the start of the DCCP header, in 32-bit words
 * @dccph_ccval - Used by the HC-Sender CCID
 * @dccph_cscov - Parts of the packet that are covered by the Checksum field
 * @dccph_checksum - Internet checksum, depends on dccph_cscov
 * @dccph_x - 0 = 24 bit sequence number, 1 = 48
 * @dccph_type - packet type, see DCCP_PKT_ prefixed macros
 * @dccph_seq - sequence number high or low order 24 bits, depends on dccph_x
 */
struct dccp_hdr {
	__be16	dccph_sport,
		dccph_dport;
	__u8	dccph_doff;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8	dccph_cscov:4,
		dccph_ccval:4;
#elif defined(__BIG_ENDIAN_BITFIELD)
	__u8	dccph_ccval:4,
		dccph_cscov:4;
#else
#error  "Adjust your <asm/byteorder.h> defines"
#endif
	__sum16	dccph_checksum;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8	dccph_x:1,
		dccph_type:4,
		dccph_reserved:3;
#elif defined(__BIG_ENDIAN_BITFIELD)
	__u8	dccph_reserved:3,
		dccph_type:4,
		dccph_x:1;
#else
#error  "Adjust your <asm/byteorder.h> defines"
#endif
	__u8	dccph_seq2;
	__be16	dccph_seq;
};

/**
 * struct dccp_hdr_ext - the low bits of a 48 bit seq packet
 *
 * @dccph_seq_low - low 24 bits of a 48 bit seq packet
 */
struct dccp_hdr_ext {
	__be32	dccph_seq_low;
};

/**
 * struct dccp_hdr_request - Connection initiation request header
 *
 * @dccph_req_service - Service to which the client app wants to connect
 */
struct dccp_hdr_request {
	__be32	dccph_req_service;
};
/**
 * struct dccp_hdr_ack_bits - acknowledgment bits common to most packets
 *
 * @dccph_resp_ack_nr_high - 48 bit ack number high order bits, contains GSR
 * @dccph_resp_ack_nr_low - 48 bit ack number low order bits, contains GSR
 */
struct dccp_hdr_ack_bits {
	__be16	dccph_reserved1;
	__be16	dccph_ack_nr_high;
	__be32	dccph_ack_nr_low;
};
/**
 * struct dccp_hdr_response - Connection initiation response header
 *
 * @dccph_resp_ack - 48 bit Acknowledgment Number Subheader (5.3)
 * @dccph_resp_service - Echoes the Service Code on a received DCCP-Request
 */
struct dccp_hdr_response {
	struct dccp_hdr_ack_bits	dccph_resp_ack;
	__be32				dccph_resp_service;
};

/**
 * struct dccp_hdr_reset - Unconditionally shut down a connection
 *
 * @dccph_reset_ack - 48 bit Acknowledgment Number Subheader (5.6)
 * @dccph_reset_code - one of %dccp_reset_codes
 * @dccph_reset_data - the Data 1 ... Data 3 fields from 5.6
 */
struct dccp_hdr_reset {
	struct dccp_hdr_ack_bits	dccph_reset_ack;
	__u8				dccph_reset_code,
					dccph_reset_data[3];
};

enum dccp_pkt_type {
	DCCP_PKT_REQUEST = 0,
	DCCP_PKT_RESPONSE,
	DCCP_PKT_DATA,
	DCCP_PKT_ACK,
	DCCP_PKT_DATAACK,
	DCCP_PKT_CLOSEREQ,
	DCCP_PKT_CLOSE,
	DCCP_PKT_RESET,
	DCCP_PKT_SYNC,
	DCCP_PKT_SYNCACK,
	DCCP_PKT_INVALID,
};

#define DCCP_NR_PKT_TYPES DCCP_PKT_INVALID

static inline unsigned int dccp_packet_hdr_len(const __u8 type)
{
	if (type == DCCP_PKT_DATA)
		return 0;
	if (type == DCCP_PKT_DATAACK	||
	    type == DCCP_PKT_ACK	||
	    type == DCCP_PKT_SYNC	||
	    type == DCCP_PKT_SYNCACK	||
	    type == DCCP_PKT_CLOSE	||
	    type == DCCP_PKT_CLOSEREQ)
		return sizeof(struct dccp_hdr_ack_bits);
	if (type == DCCP_PKT_REQUEST)
		return sizeof(struct dccp_hdr_request);
	if (type == DCCP_PKT_RESPONSE)
		return sizeof(struct dccp_hdr_response);
	return sizeof(struct dccp_hdr_reset);
}
enum dccp_reset_codes {
	DCCP_RESET_CODE_UNSPECIFIED = 0,
	DCCP_RESET_CODE_CLOSED,
	DCCP_RESET_CODE_ABORTED,
	DCCP_RESET_CODE_NO_CONNECTION,
	DCCP_RESET_CODE_PACKET_ERROR,
	DCCP_RESET_CODE_OPTION_ERROR,
	DCCP_RESET_CODE_MANDATORY_ERROR,
	DCCP_RESET_CODE_CONNECTION_REFUSED,
	DCCP_RESET_CODE_BAD_SERVICE_CODE,
	DCCP_RESET_CODE_TOO_BUSY,
	DCCP_RESET_CODE_BAD_INIT_COOKIE,
	DCCP_RESET_CODE_AGGRESSION_PENALTY,

	DCCP_MAX_RESET_CODES		/* Leave at the end!  */
};

/* DCCP options */
enum {
	DCCPO_PADDING = 0,
	DCCPO_MANDATORY = 1,
	DCCPO_MIN_RESERVED = 3,
	DCCPO_MAX_RESERVED = 31,
	DCCPO_CHANGE_L = 32,
	DCCPO_CONFIRM_L = 33,
	DCCPO_CHANGE_R = 34,
	DCCPO_CONFIRM_R = 35,
	DCCPO_NDP_COUNT = 37,
	DCCPO_ACK_VECTOR_0 = 38,
	DCCPO_ACK_VECTOR_1 = 39,
	DCCPO_TIMESTAMP = 41,
	DCCPO_TIMESTAMP_ECHO = 42,
	DCCPO_ELAPSED_TIME = 43,
	DCCPO_MAX = 45,
	DCCPO_MIN_CCID_SPECIFIC = 128,
	DCCPO_MAX_CCID_SPECIFIC = 255,
};

/* DCCP CCIDS */
enum {
	DCCPC_CCID2 = 2,
	DCCPC_CCID3 = 3,
};

/* DCCP features (RFC 4340 section 6.4) */
enum dccp_feature_numbers {
	DCCPF_RESERVED = 0,
	DCCPF_CCID = 1,
	DCCPF_SHORT_SEQNOS = 2,
	DCCPF_SEQUENCE_WINDOW = 3,
	DCCPF_ECN_INCAPABLE = 4,
	DCCPF_ACK_RATIO = 5,
	DCCPF_SEND_ACK_VECTOR = 6,
	DCCPF_SEND_NDP_COUNT = 7,
	DCCPF_MIN_CSUM_COVER = 8,
	DCCPF_DATA_CHECKSUM = 9,
	/* 10-127 reserved */
	DCCPF_MIN_CCID_SPECIFIC = 128,
	DCCPF_SEND_LEV_RATE = 192,	/* RFC 4342, sec. 8.4 */
	DCCPF_MAX_CCID_SPECIFIC = 255,
};

/* DCCP socket options */
#define DCCP_SOCKOPT_PACKET_SIZE	1 /* XXX deprecated, without effect */
#define DCCP_SOCKOPT_SERVICE		2
#define DCCP_SOCKOPT_CHANGE_L		3
#define DCCP_SOCKOPT_CHANGE_R		4
#define DCCP_SOCKOPT_GET_CUR_MPS	5
#define DCCP_SOCKOPT_SERVER_TIMEWAIT	6
#define DCCP_SOCKOPT_SEND_CSCOV		10
#define DCCP_SOCKOPT_RECV_CSCOV		11
#define DCCP_SOCKOPT_AVAILABLE_CCIDS	12
#define DCCP_SOCKOPT_CCID_RX_INFO	128
#define DCCP_SOCKOPT_CCID_TX_INFO	192

/* maximum number of services provided on the same listening port */
#define DCCP_SERVICE_LIST_MAX_LEN      32

#ifdef __KERNEL__

#include <linux/in.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/uio.h>
#include <linux/workqueue.h>

#include <net/inet_connection_sock.h>
#include <net/inet_sock.h>
#include <net/inet_timewait_sock.h>
#include <net/tcp_states.h>

enum dccp_state {
	DCCP_OPEN	     = TCP_ESTABLISHED,
	DCCP_REQUESTING	     = TCP_SYN_SENT,
	DCCP_LISTEN	     = TCP_LISTEN,
	DCCP_RESPOND	     = TCP_SYN_RECV,
	/*
	 * States involved in closing a DCCP connection:
	 * 1) ACTIVE_CLOSEREQ is entered by a server sending a CloseReq.
	 *
	 * 2) CLOSING can have three different meanings (RFC 4340, 8.3):
	 *  a. Client has performed active-close, has sent a Close to the server
	 *     from state OPEN or PARTOPEN, and is waiting for the final Reset
	 *     (in this case, SOCK_DONE == 1).
	 *  b. Client is asked to perform passive-close, by receiving a CloseReq
	 *     in (PART)OPEN state. It sends a Close and waits for final Reset
	 *     (in this case, SOCK_DONE == 0).
	 *  c. Server performs an active-close as in (a), keeps TIMEWAIT state.
	 *
	 * 3) The following intermediate states are employed to give passively
	 *    closing nodes a chance to process their unread data:
	 *    - PASSIVE_CLOSE    (from OPEN => CLOSED) and
	 *    - PASSIVE_CLOSEREQ (from (PART)OPEN to CLOSING; case (b) above).
	 */
	DCCP_ACTIVE_CLOSEREQ = TCP_FIN_WAIT1,
	DCCP_PASSIVE_CLOSE   = TCP_CLOSE_WAIT,	/* any node receiving a Close */
	DCCP_CLOSING	     = TCP_CLOSING,
	DCCP_TIME_WAIT	     = TCP_TIME_WAIT,
	DCCP_CLOSED	     = TCP_CLOSE,
	DCCP_PARTOPEN	     = TCP_MAX_STATES,
	DCCP_PASSIVE_CLOSEREQ,			/* clients receiving CloseReq */
	DCCP_MAX_STATES
};

#define DCCP_STATE_MASK 0x1f

enum {
	DCCPF_OPEN	      = TCPF_ESTABLISHED,
	DCCPF_REQUESTING      = TCPF_SYN_SENT,
	DCCPF_LISTEN	      = TCPF_LISTEN,
	DCCPF_RESPOND	      = TCPF_SYN_RECV,
	DCCPF_ACTIVE_CLOSEREQ = TCPF_FIN_WAIT1,
	DCCPF_CLOSING	      = TCPF_CLOSING,
	DCCPF_TIME_WAIT	      = TCPF_TIME_WAIT,
	DCCPF_CLOSED	      = TCPF_CLOSE,
	DCCPF_PARTOPEN	      = (1 << DCCP_PARTOPEN),
};

static inline struct dccp_hdr *dccp_hdr(const struct sk_buff *skb)
{
	return (struct dccp_hdr *)skb_transport_header(skb);
}

static inline struct dccp_hdr *dccp_zeroed_hdr(struct sk_buff *skb, int headlen)
{
	skb_push(skb, headlen);
	skb_reset_transport_header(skb);
	return memset(skb_transport_header(skb), 0, headlen);
}

static inline struct dccp_hdr_ext *dccp_hdrx(const struct dccp_hdr *dh)
{
	return (struct dccp_hdr_ext *)((unsigned char *)dh + sizeof(*dh));
}

static inline unsigned int __dccp_basic_hdr_len(const struct dccp_hdr *dh)
{
	return sizeof(*dh) + (dh->dccph_x ? sizeof(struct dccp_hdr_ext) : 0);
}

static inline unsigned int dccp_basic_hdr_len(const struct sk_buff *skb)
{
	const struct dccp_hdr *dh = dccp_hdr(skb);
	return __dccp_basic_hdr_len(dh);
}

static inline __u64 dccp_hdr_seq(const struct dccp_hdr *dh)
{
	__u64 seq_nr =  ntohs(dh->dccph_seq);

	if (dh->dccph_x != 0)
		seq_nr = (seq_nr << 32) + ntohl(dccp_hdrx(dh)->dccph_seq_low);
	else
		seq_nr += (u32)dh->dccph_seq2 << 16;

	return seq_nr;
}

static inline struct dccp_hdr_request *dccp_hdr_request(struct sk_buff *skb)
{
	return (struct dccp_hdr_request *)(skb_transport_header(skb) +
					   dccp_basic_hdr_len(skb));
}

static inline struct dccp_hdr_ack_bits *dccp_hdr_ack_bits(const struct sk_buff *skb)
{
	return (struct dccp_hdr_ack_bits *)(skb_transport_header(skb) +
					    dccp_basic_hdr_len(skb));
}

static inline u64 dccp_hdr_ack_seq(const struct sk_buff *skb)
{
	const struct dccp_hdr_ack_bits *dhack = dccp_hdr_ack_bits(skb);
	return ((u64)ntohs(dhack->dccph_ack_nr_high) << 32) + ntohl(dhack->dccph_ack_nr_low);
}

static inline struct dccp_hdr_response *dccp_hdr_response(struct sk_buff *skb)
{
	return (struct dccp_hdr_response *)(skb_transport_header(skb) +
					    dccp_basic_hdr_len(skb));
}

static inline struct dccp_hdr_reset *dccp_hdr_reset(struct sk_buff *skb)
{
	return (struct dccp_hdr_reset *)(skb_transport_header(skb) +
					 dccp_basic_hdr_len(skb));
}

static inline unsigned int __dccp_hdr_len(const struct dccp_hdr *dh)
{
	return __dccp_basic_hdr_len(dh) +
	       dccp_packet_hdr_len(dh->dccph_type);
}

static inline unsigned int dccp_hdr_len(const struct sk_buff *skb)
{
	return __dccp_hdr_len(dccp_hdr(skb));
}


/* initial values for each feature */
#define DCCPF_INITIAL_SEQUENCE_WINDOW		100
#define DCCPF_INITIAL_ACK_RATIO			2
#define DCCPF_INITIAL_CCID			DCCPC_CCID2
#define DCCPF_INITIAL_SEND_ACK_VECTOR		1
/* FIXME: for now we're default to 1 but it should really be 0 */
#define DCCPF_INITIAL_SEND_NDP_COUNT		1

/**
  * struct dccp_minisock - Minimal DCCP connection representation
  *
  * Will be used to pass the state from dccp_request_sock to dccp_sock.
  *
  * @dccpms_sequence_window - Sequence Window Feature (section 7.5.2)
  * @dccpms_ccid - Congestion Control Id (CCID) (section 10)
  * @dccpms_send_ack_vector - Send Ack Vector Feature (section 11.5)
  * @dccpms_send_ndp_count - Send NDP Count Feature (7.7.2)
  * @dccpms_ack_ratio - Ack Ratio Feature (section 11.3)
  * @dccpms_pending - List of features being negotiated
  * @dccpms_conf -
  */
struct dccp_minisock {
	__u64			dccpms_sequence_window;
	__u8			dccpms_rx_ccid;
	__u8			dccpms_tx_ccid;
	__u8			dccpms_send_ack_vector;
	__u8			dccpms_send_ndp_count;
	__u8			dccpms_ack_ratio;
	struct list_head	dccpms_pending;
	struct list_head	dccpms_conf;
};

struct dccp_opt_conf {
	__u8			*dccpoc_val;
	__u8			dccpoc_len;
};

struct dccp_opt_pend {
	struct list_head	dccpop_node;
	__u8			dccpop_type;
	__u8			dccpop_feat;
	__u8		        *dccpop_val;
	__u8			dccpop_len;
	int			dccpop_conf;
	struct dccp_opt_conf    *dccpop_sc;
};

extern void dccp_minisock_init(struct dccp_minisock *dmsk);

/**
 * struct dccp_request_sock  -  represent DCCP-specific connection request
 * @dreq_inet_rsk: structure inherited from
 * @dreq_iss: initial sequence number sent on the Response (RFC 4340, 7.1)
 * @dreq_isr: initial sequence number received on the Request
 * @dreq_service: service code present on the Request (there is just one)
 * @dreq_featneg: feature negotiation options for this connection
 * The following two fields are analogous to the ones in dccp_sock:
 * @dreq_timestamp_echo: last received timestamp to echo (13.1)
 * @dreq_timestamp_echo: the time of receiving the last @dreq_timestamp_echo
 */
struct dccp_request_sock {
	struct inet_request_sock dreq_inet_rsk;
	__u64			 dreq_iss;
	__u64			 dreq_isr;
	__be32			 dreq_service;
	struct list_head	 dreq_featneg;
	__u32			 dreq_timestamp_echo;
	__u32			 dreq_timestamp_time;
};

static inline struct dccp_request_sock *dccp_rsk(const struct request_sock *req)
{
	return (struct dccp_request_sock *)req;
}

extern struct inet_timewait_death_row dccp_death_row;

extern int dccp_parse_options(struct sock *sk, struct dccp_request_sock *dreq,
			      struct sk_buff *skb);

struct dccp_options_received {
	u64	dccpor_ndp:48;
	u32	dccpor_timestamp;
	u32	dccpor_timestamp_echo;
	u32	dccpor_elapsed_time;
};

struct ccid;

enum dccp_role {
	DCCP_ROLE_UNDEFINED,
	DCCP_ROLE_LISTEN,
	DCCP_ROLE_CLIENT,
	DCCP_ROLE_SERVER,
};

struct dccp_service_list {
	__u32	dccpsl_nr;
	__be32	dccpsl_list[0];
};

#define DCCP_SERVICE_INVALID_VALUE htonl((__u32)-1)
#define DCCP_SERVICE_CODE_IS_ABSENT		0

static inline int dccp_list_has_service(const struct dccp_service_list *sl,
					const __be32 service)
{
	if (likely(sl != NULL)) {
		u32 i = sl->dccpsl_nr;
		while (i--)
			if (sl->dccpsl_list[i] == service)
				return 1;
	}
	return 0;
}

struct dccp_ackvec;

/**
 * struct dccp_sock - DCCP socket state
 *
 * @dccps_swl - sequence number window low
 * @dccps_swh - sequence number window high
 * @dccps_awl - acknowledgement number window low
 * @dccps_awh - acknowledgement number window high
 * @dccps_iss - initial sequence number sent
 * @dccps_isr - initial sequence number received
 * @dccps_osr - first OPEN sequence number received
 * @dccps_gss - greatest sequence number sent
 * @dccps_gsr - greatest valid sequence number received
 * @dccps_gar - greatest valid ack number received on a non-Sync; initialized to %dccps_iss
 * @dccps_service - first (passive sock) or unique (active sock) service code
 * @dccps_service_list - second .. last service code on passive socket
 * @dccps_timestamp_echo - latest timestamp received on a TIMESTAMP option
 * @dccps_timestamp_time - time of receiving latest @dccps_timestamp_echo
 * @dccps_l_ack_ratio - feature-local Ack Ratio
 * @dccps_r_ack_ratio - feature-remote Ack Ratio
 * @dccps_pcslen - sender   partial checksum coverage (via sockopt)
 * @dccps_pcrlen - receiver partial checksum coverage (via sockopt)
 * @dccps_ndp_count - number of Non Data Packets since last data packet
 * @dccps_mss_cache - current value of MSS (path MTU minus header sizes)
 * @dccps_rate_last - timestamp for rate-limiting DCCP-Sync (RFC 4340, 7.5.4)
 * @dccps_minisock - associated minisock (accessed via dccp_msk)
 * @dccps_featneg - tracks feature-negotiation state (mostly during handshake)
 * @dccps_hc_rx_ackvec - rx half connection ack vector
 * @dccps_hc_rx_ccid - CCID used for the receiver (or receiving half-connection)
 * @dccps_hc_tx_ccid - CCID used for the sender (or sending half-connection)
 * @dccps_options_received - parsed set of retrieved options
 * @dccps_role - role of this sock, one of %dccp_role
 * @dccps_hc_rx_insert_options - receiver wants to add options when acking
 * @dccps_hc_tx_insert_options - sender wants to add options when sending
 * @dccps_server_timewait - server holds timewait state on close (RFC 4340, 8.3)
 * @dccps_xmit_timer - timer for when CCID is not ready to send
 * @dccps_syn_rtt - RTT sample from Request/Response exchange (in usecs)
 */
struct dccp_sock {
	/* inet_connection_sock has to be the first member of dccp_sock */
	struct inet_connection_sock	dccps_inet_connection;
#define dccps_syn_rtt			dccps_inet_connection.icsk_ack.lrcvtime
	__u64				dccps_swl;
	__u64				dccps_swh;
	__u64				dccps_awl;
	__u64				dccps_awh;
	__u64				dccps_iss;
	__u64				dccps_isr;
	__u64				dccps_osr;
	__u64				dccps_gss;
	__u64				dccps_gsr;
	__u64				dccps_gar;
	__be32				dccps_service;
	__u32				dccps_mss_cache;
	struct dccp_service_list	*dccps_service_list;
	__u32				dccps_timestamp_echo;
	__u32				dccps_timestamp_time;
	__u16				dccps_l_ack_ratio;
	__u16				dccps_r_ack_ratio;
	__u16				dccps_pcslen;
	__u16				dccps_pcrlen;
	__u64				dccps_ndp_count:48;
	unsigned long			dccps_rate_last;
	struct dccp_minisock		dccps_minisock;
	struct list_head		dccps_featneg;
	struct dccp_ackvec		*dccps_hc_rx_ackvec;
	struct ccid			*dccps_hc_rx_ccid;
	struct ccid			*dccps_hc_tx_ccid;
	struct dccp_options_received	dccps_options_received;
	enum dccp_role			dccps_role:2;
	__u8				dccps_hc_rx_insert_options:1;
	__u8				dccps_hc_tx_insert_options:1;
	__u8				dccps_server_timewait:1;
	struct timer_list		dccps_xmit_timer;
};

static inline struct dccp_sock *dccp_sk(const struct sock *sk)
{
	return (struct dccp_sock *)sk;
}

static inline struct dccp_minisock *dccp_msk(const struct sock *sk)
{
	return (struct dccp_minisock *)&dccp_sk(sk)->dccps_minisock;
}

static inline const char *dccp_role(const struct sock *sk)
{
	switch (dccp_sk(sk)->dccps_role) {
	case DCCP_ROLE_UNDEFINED: return "undefined";
	case DCCP_ROLE_LISTEN:	  return "listen";
	case DCCP_ROLE_SERVER:	  return "server";
	case DCCP_ROLE_CLIENT:	  return "client";
	}
	return NULL;
}

#endif /* __KERNEL__ */

#endif /* _LINUX_DCCP_H */
