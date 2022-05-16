/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _CCID_H
#define _CCID_H
/*
 *  net/dccp/ccid.h
 *
 *  An implementation of the DCCP protocol
 *  Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *  CCID infrastructure
 */

#include <net/sock.h>
#include <linux/compiler.h>
#include <linux/dccp.h>
#include <linux/list.h>
#include <linux/module.h>

/* maximum value for a CCID (RFC 4340, 19.5) */
#define CCID_MAX		255
#define CCID_SLAB_NAME_LENGTH	32

struct tcp_info;

/**
 *  struct ccid_operations  -  Interface to Congestion-Control Infrastructure
 *
 *  @ccid_id: numerical CCID ID (up to %CCID_MAX, cf. table 5 in RFC 4340, 10.)
 *  @ccid_ccmps: the CCMPS including network/transport headers (0 when disabled)
 *  @ccid_name: alphabetical identifier string for @ccid_id
 *  @ccid_hc_{r,t}x_slab: memory pool for the receiver/sender half-connection
 *  @ccid_hc_{r,t}x_obj_size: size of the receiver/sender half-connection socket
 *
 *  @ccid_hc_{r,t}x_init: CCID-specific initialisation routine (before startup)
 *  @ccid_hc_{r,t}x_exit: CCID-specific cleanup routine (before destruction)
 *  @ccid_hc_rx_packet_recv: implements the HC-receiver side
 *  @ccid_hc_{r,t}x_parse_options: parsing routine for CCID/HC-specific options
 *  @ccid_hc_{r,t}x_insert_options: insert routine for CCID/HC-specific options
 *  @ccid_hc_tx_packet_recv: implements feedback processing for the HC-sender
 *  @ccid_hc_tx_send_packet: implements the sending part of the HC-sender
 *  @ccid_hc_tx_packet_sent: does accounting for packets in flight by HC-sender
 *  @ccid_hc_{r,t}x_get_info: INET_DIAG information for HC-receiver/sender
 *  @ccid_hc_{r,t}x_getsockopt: socket options specific to HC-receiver/sender
 */
struct ccid_operations {
	unsigned char		ccid_id;
	__u32			ccid_ccmps;
	const char		*ccid_name;
	struct kmem_cache	*ccid_hc_rx_slab,
				*ccid_hc_tx_slab;
	char			ccid_hc_rx_slab_name[CCID_SLAB_NAME_LENGTH];
	char			ccid_hc_tx_slab_name[CCID_SLAB_NAME_LENGTH];
	__u32			ccid_hc_rx_obj_size,
				ccid_hc_tx_obj_size;
	/* Interface Routines */
	int		(*ccid_hc_rx_init)(struct ccid *ccid, struct sock *sk);
	int		(*ccid_hc_tx_init)(struct ccid *ccid, struct sock *sk);
	void		(*ccid_hc_rx_exit)(struct sock *sk);
	void		(*ccid_hc_tx_exit)(struct sock *sk);
	void		(*ccid_hc_rx_packet_recv)(struct sock *sk,
						  struct sk_buff *skb);
	int		(*ccid_hc_rx_parse_options)(struct sock *sk, u8 pkt,
						    u8 opt, u8 *val, u8 len);
	int		(*ccid_hc_rx_insert_options)(struct sock *sk,
						     struct sk_buff *skb);
	void		(*ccid_hc_tx_packet_recv)(struct sock *sk,
						  struct sk_buff *skb);
	int		(*ccid_hc_tx_parse_options)(struct sock *sk, u8 pkt,
						    u8 opt, u8 *val, u8 len);
	int		(*ccid_hc_tx_send_packet)(struct sock *sk,
						  struct sk_buff *skb);
	void		(*ccid_hc_tx_packet_sent)(struct sock *sk,
						  unsigned int len);
	void		(*ccid_hc_rx_get_info)(struct sock *sk,
					       struct tcp_info *info);
	void		(*ccid_hc_tx_get_info)(struct sock *sk,
					       struct tcp_info *info);
	int		(*ccid_hc_rx_getsockopt)(struct sock *sk,
						 const int optname, int len,
						 u32 __user *optval,
						 int __user *optlen);
	int		(*ccid_hc_tx_getsockopt)(struct sock *sk,
						 const int optname, int len,
						 u32 __user *optval,
						 int __user *optlen);
};

extern struct ccid_operations ccid2_ops;
#ifdef CONFIG_IP_DCCP_CCID3
extern struct ccid_operations ccid3_ops;
#endif

int ccid_initialize_builtins(void);
void ccid_cleanup_builtins(void);

struct ccid {
	struct ccid_operations *ccid_ops;
	char		       ccid_priv[];
};

static inline void *ccid_priv(const struct ccid *ccid)
{
	return (void *)ccid->ccid_priv;
}

bool ccid_support_check(u8 const *ccid_array, u8 array_len);
int ccid_get_builtin_ccids(u8 **ccid_array, u8 *array_len);
int ccid_getsockopt_builtin_ccids(struct sock *sk, int len,
				  char __user *, int __user *);

struct ccid *ccid_new(const u8 id, struct sock *sk, bool rx);

static inline int ccid_get_current_rx_ccid(struct dccp_sock *dp)
{
	struct ccid *ccid = dp->dccps_hc_rx_ccid;

	if (ccid == NULL || ccid->ccid_ops == NULL)
		return -1;
	return ccid->ccid_ops->ccid_id;
}

static inline int ccid_get_current_tx_ccid(struct dccp_sock *dp)
{
	struct ccid *ccid = dp->dccps_hc_tx_ccid;

	if (ccid == NULL || ccid->ccid_ops == NULL)
		return -1;
	return ccid->ccid_ops->ccid_id;
}

void ccid_hc_rx_delete(struct ccid *ccid, struct sock *sk);
void ccid_hc_tx_delete(struct ccid *ccid, struct sock *sk);

/*
 * Congestion control of queued data packets via CCID decision.
 *
 * The TX CCID performs its congestion-control by indicating whether and when a
 * queued packet may be sent, using the return code of ccid_hc_tx_send_packet().
 * The following modes are supported via the symbolic constants below:
 * - timer-based pacing    (CCID returns a delay value in milliseconds);
 * - autonomous dequeueing (CCID internally schedules dccps_xmitlet).
 */

enum ccid_dequeueing_decision {
	CCID_PACKET_SEND_AT_ONCE =	 0x00000,  /* "green light": no delay */
	CCID_PACKET_DELAY_MAX =		 0x0FFFF,  /* maximum delay in msecs  */
	CCID_PACKET_DELAY =		 0x10000,  /* CCID msec-delay mode */
	CCID_PACKET_WILL_DEQUEUE_LATER = 0x20000,  /* CCID autonomous mode */
	CCID_PACKET_ERR =		 0xF0000,  /* error condition */
};

static inline int ccid_packet_dequeue_eval(const int return_code)
{
	if (return_code < 0)
		return CCID_PACKET_ERR;
	if (return_code == 0)
		return CCID_PACKET_SEND_AT_ONCE;
	if (return_code <= CCID_PACKET_DELAY_MAX)
		return CCID_PACKET_DELAY;
	return return_code;
}

static inline int ccid_hc_tx_send_packet(struct ccid *ccid, struct sock *sk,
					 struct sk_buff *skb)
{
	if (ccid->ccid_ops->ccid_hc_tx_send_packet != NULL)
		return ccid->ccid_ops->ccid_hc_tx_send_packet(sk, skb);
	return CCID_PACKET_SEND_AT_ONCE;
}

static inline void ccid_hc_tx_packet_sent(struct ccid *ccid, struct sock *sk,
					  unsigned int len)
{
	if (ccid->ccid_ops->ccid_hc_tx_packet_sent != NULL)
		ccid->ccid_ops->ccid_hc_tx_packet_sent(sk, len);
}

static inline void ccid_hc_rx_packet_recv(struct ccid *ccid, struct sock *sk,
					  struct sk_buff *skb)
{
	if (ccid->ccid_ops->ccid_hc_rx_packet_recv != NULL)
		ccid->ccid_ops->ccid_hc_rx_packet_recv(sk, skb);
}

static inline void ccid_hc_tx_packet_recv(struct ccid *ccid, struct sock *sk,
					  struct sk_buff *skb)
{
	if (ccid->ccid_ops->ccid_hc_tx_packet_recv != NULL)
		ccid->ccid_ops->ccid_hc_tx_packet_recv(sk, skb);
}

/**
 * ccid_hc_tx_parse_options  -  Parse CCID-specific options sent by the receiver
 * @pkt: type of packet that @opt appears on (RFC 4340, 5.1)
 * @opt: the CCID-specific option type (RFC 4340, 5.8 and 10.3)
 * @val: value of @opt
 * @len: length of @val in bytes
 */
static inline int ccid_hc_tx_parse_options(struct ccid *ccid, struct sock *sk,
					   u8 pkt, u8 opt, u8 *val, u8 len)
{
	if (!ccid || !ccid->ccid_ops->ccid_hc_tx_parse_options)
		return 0;
	return ccid->ccid_ops->ccid_hc_tx_parse_options(sk, pkt, opt, val, len);
}

/**
 * ccid_hc_rx_parse_options  -  Parse CCID-specific options sent by the sender
 * Arguments are analogous to ccid_hc_tx_parse_options()
 */
static inline int ccid_hc_rx_parse_options(struct ccid *ccid, struct sock *sk,
					   u8 pkt, u8 opt, u8 *val, u8 len)
{
	if (!ccid || !ccid->ccid_ops->ccid_hc_rx_parse_options)
		return 0;
	return ccid->ccid_ops->ccid_hc_rx_parse_options(sk, pkt, opt, val, len);
}

static inline int ccid_hc_rx_insert_options(struct ccid *ccid, struct sock *sk,
					    struct sk_buff *skb)
{
	if (ccid->ccid_ops->ccid_hc_rx_insert_options != NULL)
		return ccid->ccid_ops->ccid_hc_rx_insert_options(sk, skb);
	return 0;
}

static inline void ccid_hc_rx_get_info(struct ccid *ccid, struct sock *sk,
				       struct tcp_info *info)
{
	if (ccid->ccid_ops->ccid_hc_rx_get_info != NULL)
		ccid->ccid_ops->ccid_hc_rx_get_info(sk, info);
}

static inline void ccid_hc_tx_get_info(struct ccid *ccid, struct sock *sk,
				       struct tcp_info *info)
{
	if (ccid->ccid_ops->ccid_hc_tx_get_info != NULL)
		ccid->ccid_ops->ccid_hc_tx_get_info(sk, info);
}

static inline int ccid_hc_rx_getsockopt(struct ccid *ccid, struct sock *sk,
					const int optname, int len,
					u32 __user *optval, int __user *optlen)
{
	int rc = -ENOPROTOOPT;
	if (ccid != NULL && ccid->ccid_ops->ccid_hc_rx_getsockopt != NULL)
		rc = ccid->ccid_ops->ccid_hc_rx_getsockopt(sk, optname, len,
						 optval, optlen);
	return rc;
}

static inline int ccid_hc_tx_getsockopt(struct ccid *ccid, struct sock *sk,
					const int optname, int len,
					u32 __user *optval, int __user *optlen)
{
	int rc = -ENOPROTOOPT;
	if (ccid != NULL && ccid->ccid_ops->ccid_hc_tx_getsockopt != NULL)
		rc = ccid->ccid_ops->ccid_hc_tx_getsockopt(sk, optname, len,
						 optval, optlen);
	return rc;
}
#endif /* _CCID_H */
