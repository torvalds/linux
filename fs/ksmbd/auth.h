/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#ifndef __AUTH_H__
#define __AUTH_H__

#include "ntlmssp.h"

#ifdef CONFIG_SMB_SERVER_KERBEROS5
#define AUTH_GSS_LENGTH		96
#define AUTH_GSS_PADDING	0
#else
#define AUTH_GSS_LENGTH		74
#define AUTH_GSS_PADDING	6
#endif

#define CIFS_HMAC_MD5_HASH_SIZE	(16)
#define CIFS_NTHASH_SIZE	(16)

/*
 * Size of the ntlm client response
 */
#define CIFS_AUTH_RESP_SIZE		24
#define CIFS_SMB1_SIGNATURE_SIZE	8
#define CIFS_SMB1_SESSKEY_SIZE		16

#define KSMBD_AUTH_NTLMSSP	0x0001
#define KSMBD_AUTH_KRB5		0x0002
#define KSMBD_AUTH_MSKRB5	0x0004
#define KSMBD_AUTH_KRB5U2U	0x0008

struct ksmbd_session;
struct ksmbd_conn;
struct kvec;

int ksmbd_crypt_message(struct ksmbd_conn *conn, struct kvec *iov,
			unsigned int nvec, int enc);
void ksmbd_copy_gss_neg_header(void *buf);
int ksmbd_auth_ntlmv2(struct ksmbd_session *sess, struct ntlmv2_resp *ntlmv2,
		      int blen, char *domain_name, char *cryptkey);
int ksmbd_decode_ntlmssp_auth_blob(struct authenticate_message *authblob,
				   int blob_len, struct ksmbd_conn *conn,
				   struct ksmbd_session *sess);
int ksmbd_decode_ntlmssp_neg_blob(struct negotiate_message *negblob,
				  int blob_len, struct ksmbd_conn *conn);
unsigned int
ksmbd_build_ntlmssp_challenge_blob(struct challenge_message *chgblob,
				   struct ksmbd_conn *conn);
int ksmbd_krb5_authenticate(struct ksmbd_session *sess, char *in_blob,
			    int in_len,	char *out_blob, int *out_len);
int ksmbd_sign_smb2_pdu(struct ksmbd_conn *conn, char *key, struct kvec *iov,
			int n_vec, char *sig);
int ksmbd_sign_smb3_pdu(struct ksmbd_conn *conn, char *key, struct kvec *iov,
			int n_vec, char *sig);
int ksmbd_gen_smb30_signingkey(struct ksmbd_session *sess,
			       struct ksmbd_conn *conn);
int ksmbd_gen_smb311_signingkey(struct ksmbd_session *sess,
				struct ksmbd_conn *conn);
int ksmbd_gen_smb30_encryptionkey(struct ksmbd_session *sess);
int ksmbd_gen_smb311_encryptionkey(struct ksmbd_session *sess);
int ksmbd_gen_preauth_integrity_hash(struct ksmbd_conn *conn, char *buf,
				     __u8 *pi_hash);
int ksmbd_gen_sd_hash(struct ksmbd_conn *conn, char *sd_buf, int len,
		      __u8 *pi_hash);
#endif
