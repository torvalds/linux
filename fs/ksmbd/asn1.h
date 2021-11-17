/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The ASB.1/BER parsing code is derived from ip_nat_snmp_basic.c which was in
 * turn derived from the gxsnmp package by Gregory McLean & Jochen Friedrich
 *
 * Copyright (c) 2000 RP Internet (www.rpi.net.au).
 * Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#ifndef __ASN1_H__
#define __ASN1_H__

int ksmbd_decode_negTokenInit(unsigned char *security_blob, int length,
			      struct ksmbd_conn *conn);
int ksmbd_decode_negTokenTarg(unsigned char *security_blob, int length,
			      struct ksmbd_conn *conn);
int build_spnego_ntlmssp_neg_blob(unsigned char **pbuffer, u16 *buflen,
				  char *ntlm_blob, int ntlm_blob_len);
int build_spnego_ntlmssp_auth_blob(unsigned char **pbuffer, u16 *buflen,
				   int neg_result);
#endif /* __ASN1_H__ */
