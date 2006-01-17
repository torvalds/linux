#ifndef _CCID_H
#define _CCID_H
/*
 *  net/dccp/ccid.h
 *
 *  An implementation of the DCCP protocol
 *  Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *  CCID infrastructure
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 */

#include <net/sock.h>
#include <linux/compiler.h>
#include <linux/dccp.h>
#include <linux/list.h>
#include <linux/module.h>

#define CCID_MAX 255

struct tcp_info;

struct ccid {
	unsigned char	ccid_id;
	const char	*ccid_name;
	struct module	*ccid_owner;
	int		(*ccid_init)(struct sock *sk);
	void		(*ccid_exit)(struct sock *sk);
	int		(*ccid_hc_rx_init)(struct sock *sk);
	int		(*ccid_hc_tx_init)(struct sock *sk);
	void		(*ccid_hc_rx_exit)(struct sock *sk);
	void		(*ccid_hc_tx_exit)(struct sock *sk);
	void		(*ccid_hc_rx_packet_recv)(struct sock *sk,
						  struct sk_buff *skb);
	int		(*ccid_hc_rx_parse_options)(struct sock *sk,
						    unsigned char option,
						    unsigned char len, u16 idx,
						    unsigned char* value);
	void		(*ccid_hc_rx_insert_options)(struct sock *sk,
						     struct sk_buff *skb);
	void		(*ccid_hc_tx_insert_options)(struct sock *sk,
						     struct sk_buff *skb);
	void		(*ccid_hc_tx_packet_recv)(struct sock *sk,
						  struct sk_buff *skb);
	int		(*ccid_hc_tx_parse_options)(struct sock *sk,
						    unsigned char option,
						    unsigned char len, u16 idx,
						    unsigned char* value);
	int		(*ccid_hc_tx_send_packet)(struct sock *sk,
						  struct sk_buff *skb, int len);
	void		(*ccid_hc_tx_packet_sent)(struct sock *sk, int more,
						  int len);
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

extern int	   ccid_register(struct ccid *ccid);
extern int	   ccid_unregister(struct ccid *ccid);

extern struct ccid *ccid_init(unsigned char id, struct sock *sk);
extern void	   ccid_exit(struct ccid *ccid, struct sock *sk);

static inline void __ccid_get(struct ccid *ccid)
{
	__module_get(ccid->ccid_owner);
}

static inline int ccid_hc_tx_send_packet(struct ccid *ccid, struct sock *sk,
					 struct sk_buff *skb, int len)
{
	int rc = 0;
	if (ccid->ccid_hc_tx_send_packet != NULL)
		rc = ccid->ccid_hc_tx_send_packet(sk, skb, len);
	return rc;
}

static inline void ccid_hc_tx_packet_sent(struct ccid *ccid, struct sock *sk,
					  int more, int len)
{
	if (ccid->ccid_hc_tx_packet_sent != NULL)
		ccid->ccid_hc_tx_packet_sent(sk, more, len);
}

static inline int ccid_hc_rx_init(struct ccid *ccid, struct sock *sk)
{
	int rc = 0;
	if (ccid->ccid_hc_rx_init != NULL)
		rc = ccid->ccid_hc_rx_init(sk);
	return rc;
}

static inline int ccid_hc_tx_init(struct ccid *ccid, struct sock *sk)
{
	int rc = 0;
	if (ccid->ccid_hc_tx_init != NULL)
		rc = ccid->ccid_hc_tx_init(sk);
	return rc;
}

static inline void ccid_hc_rx_exit(struct ccid *ccid, struct sock *sk)
{
	if (ccid != NULL && ccid->ccid_hc_rx_exit != NULL &&
	    dccp_sk(sk)->dccps_hc_rx_ccid_private != NULL)
		ccid->ccid_hc_rx_exit(sk);
}

static inline void ccid_hc_tx_exit(struct ccid *ccid, struct sock *sk)
{
	if (ccid != NULL && ccid->ccid_hc_tx_exit != NULL &&
	    dccp_sk(sk)->dccps_hc_tx_ccid_private != NULL)
		ccid->ccid_hc_tx_exit(sk);
}

static inline void ccid_hc_rx_packet_recv(struct ccid *ccid, struct sock *sk,
					  struct sk_buff *skb)
{
	if (ccid->ccid_hc_rx_packet_recv != NULL)
		ccid->ccid_hc_rx_packet_recv(sk, skb);
}

static inline void ccid_hc_tx_packet_recv(struct ccid *ccid, struct sock *sk,
					  struct sk_buff *skb)
{
	if (ccid->ccid_hc_tx_packet_recv != NULL)
		ccid->ccid_hc_tx_packet_recv(sk, skb);
}

static inline int ccid_hc_tx_parse_options(struct ccid *ccid, struct sock *sk,
					   unsigned char option,
					   unsigned char len, u16 idx,
					   unsigned char* value)
{
	int rc = 0;
	if (ccid->ccid_hc_tx_parse_options != NULL)
		rc = ccid->ccid_hc_tx_parse_options(sk, option, len, idx,
						    value);
	return rc;
}

static inline int ccid_hc_rx_parse_options(struct ccid *ccid, struct sock *sk,
					   unsigned char option,
					   unsigned char len, u16 idx,
					   unsigned char* value)
{
	int rc = 0;
	if (ccid->ccid_hc_rx_parse_options != NULL)
		rc = ccid->ccid_hc_rx_parse_options(sk, option, len, idx, value);
	return rc;
}

static inline void ccid_hc_tx_insert_options(struct ccid *ccid, struct sock *sk,
					     struct sk_buff *skb)
{
	if (ccid->ccid_hc_tx_insert_options != NULL)
		ccid->ccid_hc_tx_insert_options(sk, skb);
}

static inline void ccid_hc_rx_insert_options(struct ccid *ccid, struct sock *sk,
					     struct sk_buff *skb)
{
	if (ccid->ccid_hc_rx_insert_options != NULL)
		ccid->ccid_hc_rx_insert_options(sk, skb);
}

static inline void ccid_hc_rx_get_info(struct ccid *ccid, struct sock *sk,
				       struct tcp_info *info)
{
	if (ccid->ccid_hc_rx_get_info != NULL)
		ccid->ccid_hc_rx_get_info(sk, info);
}

static inline void ccid_hc_tx_get_info(struct ccid *ccid, struct sock *sk,
				       struct tcp_info *info)
{
	if (ccid->ccid_hc_tx_get_info != NULL)
		ccid->ccid_hc_tx_get_info(sk, info);
}

static inline int ccid_hc_rx_getsockopt(struct ccid *ccid, struct sock *sk,
					const int optname, int len,
					u32 __user *optval, int __user *optlen)
{
	int rc = -ENOPROTOOPT;
	if (ccid->ccid_hc_rx_getsockopt != NULL)
		rc = ccid->ccid_hc_rx_getsockopt(sk, optname, len,
						 optval, optlen);
	return rc;
}

static inline int ccid_hc_tx_getsockopt(struct ccid *ccid, struct sock *sk,
					const int optname, int len,
					u32 __user *optval, int __user *optlen)
{
	int rc = -ENOPROTOOPT;
	if (ccid->ccid_hc_tx_getsockopt != NULL)
		rc = ccid->ccid_hc_tx_getsockopt(sk, optname, len,
						 optval, optlen);
	return rc;
}
#endif /* _CCID_H */
