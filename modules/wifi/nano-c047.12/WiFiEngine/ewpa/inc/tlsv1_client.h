/*
 * wpa_supplicant: TLSv1 client (RFC 2246)
 * Copyright (c) 2006, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef TLSV1_CLIENT_H
#define TLSV1_CLIENT_H

struct tlsv1_client;

int tlsv1_client_global_init(void);
void tlsv1_client_global_deinit(void);
struct tlsv1_client * tlsv1_client_init(void);
void tlsv1_client_deinit(struct tlsv1_client *conn);
int tlsv1_client_established(struct tlsv1_client *conn);
int tlsv1_client_prf(struct tlsv1_client *conn, const char *label,
		     int server_random_first, u8 *out, size_t out_len);
u8 * tlsv1_client_handshake(struct tlsv1_client *conn,
			    const u8 *in_data, size_t in_len,
			    size_t *out_len);
int tlsv1_client_encrypt(struct tlsv1_client *conn,
			 const u8 *in_data, size_t in_len,
			 u8 *out_data, size_t out_len);
int tlsv1_client_decrypt(struct tlsv1_client *conn,
			 const u8 *in_data, size_t in_len,
			 u8 *out_data, size_t out_len);
int tlsv1_client_get_cipher(struct tlsv1_client *conn, char *buf,
			    size_t buflen);
int tlsv1_client_shutdown(struct tlsv1_client *conn);
int tlsv1_client_resumed(struct tlsv1_client *conn);
int tlsv1_client_hello_ext(struct tlsv1_client *conn, int ext_type,
			   const u8 *data, size_t data_len);
int tlsv1_client_get_keys(struct tlsv1_client *conn, struct tls_keys *keys);
int tlsv1_client_set_master_key(struct tlsv1_client *conn,
				const u8 *key, size_t key_len);
int tlsv1_client_get_keyblock_size(struct tlsv1_client *conn);
int tlsv1_client_set_cipher_list(struct tlsv1_client *conn, u8 *ciphers);
int tlsv1_client_set_ca_cert(struct tlsv1_client *conn, const char *cert,
			     const u8 *cert_blob, size_t cert_blob_len,
			     const char *path);
int tlsv1_client_set_client_cert(struct tlsv1_client *conn, const char *cert,
				 const u8 *cert_blob, size_t cert_blob_len);
int tlsv1_client_set_private_key(struct tlsv1_client *conn,
				 const char *private_key,
				 const char *private_key_passwd,
				 const u8 *private_key_blob,
				 size_t private_key_blob_len);

#endif /* TLSV1_CLIENT_H */
