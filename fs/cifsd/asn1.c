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
#include "spnego_negtokeninit.asn1.h"
#include "spnego_negtokentarg.asn1.h"

#define SPNEGO_OID_LEN 7
#define NTLMSSP_OID_LEN  10
#define KRB5_OID_LEN  7
#define KRB5U2U_OID_LEN  8
#define MSKRB5_OID_LEN  7
static unsigned long SPNEGO_OID[7] = { 1, 3, 6, 1, 5, 5, 2 };
static unsigned long NTLMSSP_OID[10] = { 1, 3, 6, 1, 4, 1, 311, 2, 2, 10 };
static unsigned long KRB5_OID[7] = { 1, 2, 840, 113554, 1, 2, 2 };
static unsigned long KRB5U2U_OID[8] = { 1, 2, 840, 113554, 1, 2, 2, 3 };
static unsigned long MSKRB5_OID[7] = { 1, 2, 840, 48018, 1, 2, 2 };

static char NTLMSSP_OID_STR[NTLMSSP_OID_LEN] = { 0x2b, 0x06, 0x01, 0x04, 0x01,
	0x82, 0x37, 0x02, 0x02, 0x0a };

static bool
asn1_subid_decode(const unsigned char **begin, const unsigned char *end,
		unsigned long *subid)
{
	const unsigned char *ptr = *begin;
	unsigned char ch;

	*subid = 0;

	do {
		if (ptr >= end)
			return false;

		ch = *ptr++;
		*subid <<= 7;
		*subid |= ch & 0x7F;
	} while ((ch & 0x80) == 0x80);

	*begin = ptr;
	return true;
}

static bool asn1_oid_decode(const unsigned char *value, size_t vlen,
		unsigned long **oid, size_t *oidlen)
{
	const unsigned char *iptr = value, *end = value + vlen;
	unsigned long *optr;
	unsigned long subid;

	vlen += 1;
	if (vlen < 2 || vlen > UINT_MAX/sizeof(unsigned long))
		return false;

	*oid = kmalloc(vlen * sizeof(unsigned long), GFP_KERNEL);
	if (!*oid)
		return false;

	optr = *oid;

	if (!asn1_subid_decode(&iptr, end, &subid)) {
		kfree(*oid);
		*oid = NULL;
		return false;
	}

	if (subid < 40) {
		optr[0] = 0;
		optr[1] = subid;
	} else if (subid < 80) {
		optr[0] = 1;
		optr[1] = subid - 40;
	} else {
		optr[0] = 2;
		optr[1] = subid - 80;
	}

	*oidlen = 2;
	optr += 2;

	while (iptr < end) {
		if (++(*oidlen) > vlen) {
			kfree(*oid);
			*oid = NULL;
			return false;
		}

		if (!asn1_subid_decode(&iptr, end, optr++)) {
			kfree(*oid);
			*oid = NULL;
			return false;
		}
	}
	return true;
}

static bool
oid_eq(unsigned long *oid1, unsigned int oid1len,
		unsigned long *oid2, unsigned int oid2len)
{
	unsigned int i;

	if (oid1len != oid2len)
		return false;

	for (i = 0; i < oid1len; i++) {
		if (oid1[i] != oid2[i])
			return false;
	}
	return true;
}

int
ksmbd_decode_negTokenInit(unsigned char *security_blob, int length,
		struct ksmbd_conn *conn)
{
	return asn1_ber_decoder(&spnego_negtokeninit_decoder, conn,
				security_blob, length);
}

int
ksmbd_decode_negTokenTarg(unsigned char *security_blob, int length,
		struct ksmbd_conn *conn)
{
	return asn1_ber_decoder(&spnego_negtokentarg_decoder, conn,
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

static void encode_asn_tag(char *buf,
			   unsigned int *ofs,
			   char tag,
			   char seq,
			   int length)
{
	int i;
	int index = *ofs;
	char hdr_len = compute_asn_hdr_len_bytes(length);
	int len = length + 2 + hdr_len;

	/* insert tag */
	buf[index++] = tag;

	if (!hdr_len)
		buf[index++] = len;
	else {
		buf[index++] = 0x80 | hdr_len;
		for (i = hdr_len - 1; i >= 0; i--)
			buf[index++] = (len >> (i * 8)) & 0xFF;
	}

	/* insert seq */
	len = len - (index - *ofs);
	buf[index++] = seq;

	if (!hdr_len)
		buf[index++] = len;
	else {
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

int gssapi_this_mech(void *context, size_t hdrlen,
		unsigned char tag, const void *value, size_t vlen)
{
	unsigned long *oid;
	size_t oidlen;
	int err = 0;

	if (!asn1_oid_decode(value, vlen, &oid, &oidlen)) {
		err = -EBADMSG;
		goto out;
	}

	if (!oid_eq(oid, oidlen, SPNEGO_OID, SPNEGO_OID_LEN))
		err = -EBADMSG;
	kfree(oid);
out:
	if (err) {
		char buf[50];

		sprint_oid(value, vlen, buf, sizeof(buf));
		ksmbd_debug(AUTH, "Unexpected OID: %s\n", buf);
	}
	return err;
}

int neg_token_init_mech_type(void *context, size_t hdrlen,
		unsigned char tag, const void *value, size_t vlen)
{
	struct ksmbd_conn *conn = context;
	unsigned long *oid;
	size_t oidlen;
	int mech_type;

	if (!asn1_oid_decode(value, vlen, &oid, &oidlen)) {
		char buf[50];

		sprint_oid(value, vlen, buf, sizeof(buf));
		ksmbd_debug(AUTH, "Unexpected OID: %s\n", buf);
		return -EBADMSG;
	}

	if (oid_eq(oid, oidlen, NTLMSSP_OID, NTLMSSP_OID_LEN))
		mech_type = KSMBD_AUTH_NTLMSSP;
	else if (oid_eq(oid, oidlen, MSKRB5_OID, MSKRB5_OID_LEN))
		mech_type = KSMBD_AUTH_MSKRB5;
	else if (oid_eq(oid, oidlen, KRB5_OID, KRB5_OID_LEN))
		mech_type = KSMBD_AUTH_KRB5;
	else if (oid_eq(oid, oidlen, KRB5U2U_OID, KRB5U2U_OID_LEN))
		mech_type = KSMBD_AUTH_KRB5U2U;
	else
		goto out;

	conn->auth_mechs |= mech_type;
	if (conn->preferred_auth_mech == 0)
		conn->preferred_auth_mech = mech_type;

out:
	kfree(oid);
	return 0;
}

int neg_token_init_mech_token(void *context, size_t hdrlen,
		unsigned char tag, const void *value, size_t vlen)
{
	struct ksmbd_conn *conn = context;

	conn->mechToken = kmalloc(vlen + 1, GFP_KERNEL);
	if (!conn->mechToken)
		return -ENOMEM;

	memcpy(conn->mechToken, value, vlen);
	conn->mechToken[vlen] = '\0';
	return 0;
}

int neg_token_targ_resp_token(void *context, size_t hdrlen,
		unsigned char tag, const void *value, size_t vlen)
{
	struct ksmbd_conn *conn = context;

	conn->mechToken = kmalloc(vlen + 1, GFP_KERNEL);
	if (!conn->mechToken)
		return -ENOMEM;

	memcpy(conn->mechToken, value, vlen);
	conn->mechToken[vlen] = '\0';
	return 0;
}
