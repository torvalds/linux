/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Generic netlink HANDSHAKE service.
 *
 * Author: Chuck Lever <chuck.lever@oracle.com>
 *
 * Copyright (c) 2023, Oracle and/or its affiliates.
 */

#ifndef _NET_HANDSHAKE_H
#define _NET_HANDSHAKE_H

enum {
	TLS_NO_KEYRING = 0,
	TLS_NO_PEERID = 0,
	TLS_NO_CERT = 0,
	TLS_NO_PRIVKEY = 0,
};

typedef void	(*tls_done_func_t)(void *data, int status,
				   key_serial_t peerid);

struct tls_handshake_args {
	struct socket		*ta_sock;
	tls_done_func_t		ta_done;
	void			*ta_data;
	const char		*ta_peername;
	unsigned int		ta_timeout_ms;
	key_serial_t		ta_keyring;
	key_serial_t		ta_my_cert;
	key_serial_t		ta_my_privkey;
	unsigned int		ta_num_peerids;
	key_serial_t		ta_my_peerids[5];
};

int tls_client_hello_anon(const struct tls_handshake_args *args, gfp_t flags);
int tls_client_hello_x509(const struct tls_handshake_args *args, gfp_t flags);
int tls_client_hello_psk(const struct tls_handshake_args *args, gfp_t flags);
int tls_server_hello_x509(const struct tls_handshake_args *args, gfp_t flags);
int tls_server_hello_psk(const struct tls_handshake_args *args, gfp_t flags);

bool tls_handshake_cancel(struct sock *sk);
void tls_handshake_close(struct socket *sock);

u8 tls_get_record_type(const struct sock *sk, const struct cmsghdr *msg);
void tls_alert_recv(const struct sock *sk, const struct msghdr *msg,
		    u8 *level, u8 *description);

#endif /* _NET_HANDSHAKE_H */
