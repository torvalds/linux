/*	$OpenBSD: ct_prn.c,v 1.7 2023/07/08 07:22:58 beck Exp $ */
/*
 * Written by Rob Stradling (rob@comodo.com) and Stephen Henson
 * (steve@openssl.org) for the OpenSSL project 2014.
 */
/* ====================================================================
 * Copyright (c) 2014 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */

#ifdef OPENSSL_NO_CT
# error "CT is disabled"
#endif

#include <openssl/asn1.h>
#include <openssl/bio.h>

#include "ct_local.h"

/*
 * XXX public api in OpenSSL 1.1.0  but this is the only thing that uses it.
 * so I am stuffing it here for the moment.
 */
static int
BIO_hex_string(BIO *out, int indent, int width, unsigned char *data,
    int datalen)
{
	int i, j = 0;

	if (datalen < 1)
		return 1;

	for (i = 0; i < datalen - 1; i++) {
		if (i && !j)
			BIO_printf(out, "%*s", indent, "");

		BIO_printf(out, "%02X:", data[i]);

		j = (j + 1) % width;
		if (!j)
			BIO_printf(out, "\n");
	}

	if (i && !j)
		BIO_printf(out, "%*s", indent, "");
	BIO_printf(out, "%02X", data[datalen - 1]);
	return 1;
}

static void
SCT_signature_algorithms_print(const SCT *sct, BIO *out)
{
	int nid = SCT_get_signature_nid(sct);

	if (nid == NID_undef)
		BIO_printf(out, "%02X%02X", sct->hash_alg, sct->sig_alg);
	else
		BIO_printf(out, "%s", OBJ_nid2ln(nid));
}

static void
timestamp_print(uint64_t timestamp, BIO *out)
{
	ASN1_GENERALIZEDTIME *gen = ASN1_GENERALIZEDTIME_new();
	char genstr[20];

	if (gen == NULL)
		return;
	ASN1_GENERALIZEDTIME_adj(gen, (time_t)0, (int)(timestamp / 86400000),
	    (timestamp % 86400000) / 1000);
	/*
	 * Note GeneralizedTime from ASN1_GENERALIZETIME_adj is always 15
	 * characters long with a final Z. Update it with fractional seconds.
	 */
	snprintf(genstr, sizeof(genstr), "%.14sZ", ASN1_STRING_get0_data(gen));
	if (ASN1_GENERALIZEDTIME_set_string(gen, genstr))
		ASN1_GENERALIZEDTIME_print(out, gen);
	ASN1_GENERALIZEDTIME_free(gen);
}

const char *
SCT_validation_status_string(const SCT *sct)
{
	switch (SCT_get_validation_status(sct)) {
	case SCT_VALIDATION_STATUS_NOT_SET:
		return "not set";
	case SCT_VALIDATION_STATUS_UNKNOWN_VERSION:
		return "unknown version";
	case SCT_VALIDATION_STATUS_UNKNOWN_LOG:
		return "unknown log";
	case SCT_VALIDATION_STATUS_UNVERIFIED:
		return "unverified";
	case SCT_VALIDATION_STATUS_INVALID:
		return "invalid";
	case SCT_VALIDATION_STATUS_VALID:
		return "valid";
	}
	return "unknown status";
}
LCRYPTO_ALIAS(SCT_validation_status_string);

void
SCT_print(const SCT *sct, BIO *out, int indent, const CTLOG_STORE *log_store)
{
	const CTLOG *log = NULL;

	if (log_store != NULL) {
		log = CTLOG_STORE_get0_log_by_id(log_store, sct->log_id,
		    sct->log_id_len);
	}

	BIO_printf(out, "%*sSigned Certificate Timestamp:", indent, "");
	BIO_printf(out, "\n%*sVersion   : ", indent + 4, "");

	if (sct->version != SCT_VERSION_V1) {
		BIO_printf(out, "unknown\n%*s", indent + 16, "");
		BIO_hex_string(out, indent + 16, 16, sct->sct, sct->sct_len);
		return;
	}

	BIO_printf(out, "v1 (0x0)");

	if (log != NULL) {
		BIO_printf(out, "\n%*sLog       : %s", indent + 4, "",
		    CTLOG_get0_name(log));
	}

	BIO_printf(out, "\n%*sLog ID    : ", indent + 4, "");
	BIO_hex_string(out, indent + 16, 16, sct->log_id, sct->log_id_len);

	BIO_printf(out, "\n%*sTimestamp : ", indent + 4, "");
	timestamp_print(sct->timestamp, out);

	BIO_printf(out, "\n%*sExtensions: ", indent + 4, "");
	if (sct->ext_len == 0)
		BIO_printf(out, "none");
	else
		BIO_hex_string(out, indent + 16, 16, sct->ext, sct->ext_len);

	BIO_printf(out, "\n%*sSignature : ", indent + 4, "");
	SCT_signature_algorithms_print(sct, out);
	BIO_printf(out, "\n%*s            ", indent + 4, "");
	BIO_hex_string(out, indent + 16, 16, sct->sig, sct->sig_len);
}
LCRYPTO_ALIAS(SCT_print);

void
SCT_LIST_print(const STACK_OF(SCT) *sct_list, BIO *out, int indent,
    const char *separator, const CTLOG_STORE *log_store)
{
	int sct_count = sk_SCT_num(sct_list);
	int i;

	for (i = 0; i < sct_count; ++i) {
		SCT *sct = sk_SCT_value(sct_list, i);

		SCT_print(sct, out, indent, log_store);
		if (i < sk_SCT_num(sct_list) - 1)
			BIO_printf(out, "%s", separator);
	}
}
LCRYPTO_ALIAS(SCT_LIST_print);
