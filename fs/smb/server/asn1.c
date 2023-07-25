// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The ASB.1/BER parsing code is derived from ip_nat_snmp_basic.c which was in
 * turn derived from the gxsnmp package by Gregory McLean & Jochen Friedrich
 *
 * Copyright (c) 2000 RP Internet (www.rpi.net.au).
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/oid_registry.h>

#include "glob.h"

#include "asn1.h"
#include "connection.h"
#include "auth.h"
#include "ksmbd_spnego_negtokeninit.asn1.h"
#include "ksmbd_spnego_negtokentarg.asn1.h"

#define NTLMSSP_OID_LEN  10

static char NTLMSSP_OID_STR[NTLMSSP_OID_LEN] = { 0x2b, 0x06, 0x01, 0x04, 0x01,
	0x82, 0x37, 0x02, 0x02, 0x0a };

int
ksmbd_decode_negTokenInit(unsigned char *security_blob, int length,
			  struct ksmbd_conn *conn)
{
	return asn1_ber_decoder(&ksmbd_spnego_negtokeninit_decoder, conn,
				security_blob, length);
}

int
ksmbd_decode_negTokenTarg(unsigned char *security_blob, int length,
			  struct ksmbd_conn *conn)
{
	return asn1_ber_decoder(&ksmbd_spnego_negtokentarg_decoder, conn,
				security_blob, length);
}

static int compute_asn_hdr_len_bytes(int len)
{
	if (len > 0xFFFFFF)
		return 4;
	else if (len > 0xFFFF)
		return 3;
	else if (len > 0xFF)
		return 2;
	else if (len > 0x7F)
		return 1;
	else
		return 0;
}

static void encode_asn_tag(char *buf, unsigned int *ofs, char tag, char seq,
			   int length)
{
	int i;
	int index = *ofs;
	char hdr_len = compute_asn_hdr_len_bytes(length);
	int len = length + 2 + hdr_len;

	/* insert tag */
	buf[index++] = tag;

	if (!hdr_len) {
		buf[index++] = len;
	} else {
		buf[index++] = 0x80 | hdr_len;
		for (i = hdr_len - 1; i >= 0; i--)
			buf[index++] = (len >> (i * 8)) & 0xFF;
	}

	/* insert seq */
	len = len - (index - *ofs);
	buf[index++] = seq;

	if (!hdr_len) {
		buf[index++] = len;
	} else {
		buf[index++] = 0x80 | hdr_len;
		for (i = hdr_len - 1; i >= 0; i--)
			buf[index++] = (len >> (i * 8)) & 0xFF;
	}

	*ofs += (index - *ofs);
}

int build_spnego_ntlmssp_neg_blob(unsigned char **pbuffer, u16 *buflen,
				  char *ntlm_blob, int ntlm_blob_len)
{
	char *buf;
	unsigned int ofs = 0;
	int neg_result_len = 4 + compute_asn_hdr_len_bytes(1) * 2 + 1;
	int oid_len = 4 + compute_asn_hdr_len_bytes(NTLMSSP_OID_LEN) * 2 +
		NTLMSSP_OID_LEN;
	int ntlmssp_len = 4 + compute_asn_hdr_len_bytes(ntlm_blob_len) * 2 +
		ntlm_blob_len;
	int total_len = 4 + compute_asn_hdr_len_bytes(neg_result_len +
			oid_len + ntlmssp_len) * 2 +
			neg_result_len + oid_len + ntlmssp_len;

	buf = kmalloc(total_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* insert main gss header */
	encode_asn_tag(buf, &ofs, 0xa1, 0x30, neg_result_len + oid_len +
			ntlmssp_len);

	/* insert neg result */
	encode_asn_tag(buf, &ofs, 0xa0, 0x0a, 1);
	buf[ofs++] = 1;

	/* insert oid */
	encode_asn_tag(buf, &ofs, 0xa1, 0x06, NTLMSSP_OID_LEN);
	memcpy(buf + ofs, NTLMSSP_OID_STR, NTLMSSP_OID_LEN);
	ofs += NTLMSSP_OID_LEN;

	/* insert response token - ntlmssp blob */
	encode_asn_tag(buf, &ofs, 0xa2, 0x04, ntlm_blob_len);
	memcpy(buf + ofs, ntlm_blob, ntlm_blob_len);
	ofs += ntlm_blob_len;

	*pbuffer = buf;
	*buflen = total_len;
	return 0;
}

int build_spnego_ntlmssp_auth_blob(unsigned char **pbuffer, u16 *buflen,
				   int neg_result)
{
	char *buf;
	unsigned int ofs = 0;
	int neg_result_len = 4 + compute_asn_hdr_len_bytes(1) * 2 + 1;
	int total_len = 4 + compute_asn_hdr_len_bytes(neg_result_len) * 2 +
		neg_result_len;

	buf = kmalloc(total_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* insert main gss header */
	encode_asn_tag(buf, &ofs, 0xa1, 0x30, neg_result_len);

	/* insert neg result */
	encode_asn_tag(buf, &ofs, 0xa0, 0x0a, 1);
	if (neg_result)
		buf[ofs++] = 2;
	else
		buf[ofs++] = 0;

	*pbuffer = buf;
	*buflen = total_len;
	return 0;
}

int ksmbd_gssapi_this_mech(void *context, size_t hdrlen, unsigned char tag,
			   const void *value, size_t vlen)
{
	enum OID oid;

	oid = look_up_OID(value, vlen);
	if (oid != OID_spnego) {
		char buf[50];

		sprint_oid(value, vlen, buf, sizeof(buf));
		ksmbd_debug(AUTH, "Unexpected OID: %s\n", buf);
		return -EBADMSG;
	}

	return 0;
}

int ksmbd_neg_token_init_mech_type(void *context, size_t hdrlen,
				   unsigned char tag, const void *value,
				   size_t vlen)
{
	struct ksmbd_conn *conn = context;
	enum OID oid;
	int mech_type;

	oid = look_up_OID(value, vlen);
	if (oid == OID_ntlmssp) {
		mech_type = KSMBD_AUTH_NTLMSSP;
	} else if (oid == OID_mskrb5) {
		mech_type = KSMBD_AUTH_MSKRB5;
	} else if (oid == OID_krb5) {
		mech_type = KSMBD_AUTH_KRB5;
	} else if (oid == OID_krb5u2u) {
		mech_type = KSMBD_AUTH_KRB5U2U;
	} else {
		char buf[50];

		sprint_oid(value, vlen, buf, sizeof(buf));
		ksmbd_debug(AUTH, "Unexpected OID: %s\n", buf);
		return -EBADMSG;
	}

	conn->auth_mechs |= mech_type;
	if (conn->preferred_auth_mech == 0)
		conn->preferred_auth_mech = mech_type;

	return 0;
}

static int ksmbd_neg_token_alloc(void *context, size_t hdrlen,
				 unsigned char tag, const void *value,
				 size_t vlen)
{
	struct ksmbd_conn *conn = context;

	conn->mechToken = kmemdup_nul(value, vlen, GFP_KERNEL);
	if (!conn->mechToken)
		return -ENOMEM;

	return 0;
}

int ksmbd_neg_token_init_mech_token(void *context, size_t hdrlen,
				    unsigned char tag, const void *value,
				    size_t vlen)
{
	return ksmbd_neg_token_alloc(context, hdrlen, tag, value, vlen);
}

int ksmbd_neg_token_targ_resp_token(void *context, size_t hdrlen,
				    unsigned char tag, const void *value,
				    size_t vlen)
{
	return ksmbd_neg_token_alloc(context, hdrlen, tag, value, vlen);
}
