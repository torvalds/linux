/* $OpenBSD: a_strnid.c,v 1.32 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 1999.
 */
/* ====================================================================
 * Copyright (c) 1999 The OpenSSL Project.  All rights reserved.
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

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/objects.h>

/*
 * XXX - unprotected global state
 *
 * This is the global mask for the mbstring functions: this is used to
 * mask out certain types (such as BMPString and UTF8String) because
 * certain software (e.g. Netscape) has problems with them.
 */
static unsigned long global_mask = B_ASN1_UTF8STRING;

void
ASN1_STRING_set_default_mask(unsigned long mask)
{
	global_mask = mask;
}
LCRYPTO_ALIAS(ASN1_STRING_set_default_mask);

unsigned long
ASN1_STRING_get_default_mask(void)
{
	return global_mask;
}
LCRYPTO_ALIAS(ASN1_STRING_get_default_mask);

/*
 * This function sets the default to various "flavours" of configuration
 * based on an ASCII string. Currently this is:
 * MASK:XXXX : a numerical mask value.
 * nobmp : Don't use BMPStrings (just Printable, T61).
 * pkix : PKIX recommendation in RFC2459.
 * utf8only : only use UTF8Strings (RFC2459 recommendation for 2004).
 * default:   the default value, Printable, T61, BMP.
 */

int
ASN1_STRING_set_default_mask_asc(const char *p)
{
	unsigned long mask;
	char *end;
	int save_errno;

	if (strncmp(p, "MASK:", 5) == 0) {
		if (p[5] == '\0')
			return 0;
		save_errno = errno;
		errno = 0;
		mask = strtoul(p + 5, &end, 0);
		if (errno == ERANGE && mask == ULONG_MAX)
			return 0;
		errno = save_errno;
		if (*end != '\0')
			return 0;
	} else if (strcmp(p, "nombstr") == 0)
		mask = ~((unsigned long)(B_ASN1_BMPSTRING|B_ASN1_UTF8STRING));
	else if (strcmp(p, "pkix") == 0)
		mask = ~((unsigned long)B_ASN1_T61STRING);
	else if (strcmp(p, "utf8only") == 0)
		mask = B_ASN1_UTF8STRING;
	else if (strcmp(p, "default") == 0)
		mask = 0xFFFFFFFFL;
	else
		return 0;
	ASN1_STRING_set_default_mask(mask);
	return 1;
}
LCRYPTO_ALIAS(ASN1_STRING_set_default_mask_asc);

/*
 * The following function generates an ASN1_STRING based on limits in a table.
 * Frequently the types and length of an ASN1_STRING are restricted by a
 * corresponding OID. For example certificates and certificate requests.
 */

ASN1_STRING *
ASN1_STRING_set_by_NID(ASN1_STRING **out, const unsigned char *in, int inlen,
    int inform, int nid)
{
	const ASN1_STRING_TABLE *tbl;
	ASN1_STRING *str = NULL;
	unsigned long mask;
	int ret;

	if (out == NULL)
		out = &str;
	tbl = ASN1_STRING_TABLE_get(nid);
	if (tbl != NULL) {
		mask = tbl->mask;
		if ((tbl->flags & STABLE_NO_MASK) == 0)
			mask &= global_mask;
		ret = ASN1_mbstring_ncopy(out, in, inlen, inform, mask,
		    tbl->minsize, tbl->maxsize);
	} else
		ret = ASN1_mbstring_copy(out, in, inlen, inform,
		    DIRSTRING_TYPE & global_mask);
	if (ret <= 0)
		return NULL;
	return *out;
}
LCRYPTO_ALIAS(ASN1_STRING_set_by_NID);

/* From RFC 5280, Appendix A.1. */
#define ub_name				32768
#define ub_common_name			64
#define ub_locality_name		128
#define ub_state_name			128
#define ub_organization_name		64
#define ub_organization_unit_name	64
#define ub_title			64
#define ub_email_address		128 /* XXX - bumped to 255 in RFC 5280 */
#define ub_serial_number		64

static const ASN1_STRING_TABLE tbl_standard[] = {
	{
		.nid = NID_commonName,
		.minsize = 1,
		.maxsize = ub_common_name,
		.mask = DIRSTRING_TYPE,
		.flags = 0,
	},
	{
		.nid = NID_countryName,
		.minsize = 2,
		.maxsize = 2,
		.mask = B_ASN1_PRINTABLESTRING,
		.flags = STABLE_NO_MASK,
	},
	{
		.nid = NID_localityName,
		.minsize = 1,
		.maxsize = ub_locality_name,
		.mask = DIRSTRING_TYPE,
		.flags = 0,
	},
	{
		.nid = NID_stateOrProvinceName,
		.minsize = 1,
		.maxsize = ub_state_name,
		.mask = DIRSTRING_TYPE,
		.flags = 0,
	},
	{
		.nid = NID_organizationName,
		.minsize = 1,
		.maxsize = ub_organization_name,
		.mask = DIRSTRING_TYPE,
		.flags = 0,
	},
	{
		.nid = NID_organizationalUnitName,
		.minsize = 1,
		.maxsize = ub_organization_unit_name,
		.mask = DIRSTRING_TYPE,
		.flags = 0,
	},
	{
		.nid = NID_pkcs9_emailAddress,
		.minsize = 1,
		.maxsize = ub_email_address,
		.mask = B_ASN1_IA5STRING,
		.flags = STABLE_NO_MASK,
	},
	{
		.nid = NID_pkcs9_unstructuredName,
		.minsize = 1,
		.maxsize = -1,
		.mask = PKCS9STRING_TYPE,
		.flags = 0,
	},
	{
		.nid = NID_pkcs9_challengePassword,
		.minsize = 1,
		.maxsize = -1,
		.mask = PKCS9STRING_TYPE,
		.flags = 0,
	},
	{
		.nid = NID_pkcs9_unstructuredAddress,
		.minsize = 1,
		.maxsize = -1,
		.mask = DIRSTRING_TYPE,
		.flags = 0,
	},
	{
		.nid = NID_givenName,
		.minsize = 1,
		.maxsize = ub_name,
		.mask = DIRSTRING_TYPE,
		.flags = 0,
	},
	{
		.nid = NID_surname,
		.minsize = 1,
		.maxsize = ub_name,
		.mask = DIRSTRING_TYPE,
		.flags = 0,
	},
	{
		.nid = NID_initials,
		.minsize = 1,
		.maxsize = ub_name,
		.mask = DIRSTRING_TYPE,
		.flags = 0,
	},
	{
		.nid = NID_serialNumber,
		.minsize = 1,
		.maxsize = ub_serial_number,
		.mask = B_ASN1_PRINTABLESTRING,
		.flags = STABLE_NO_MASK,
	},
	{
		.nid = NID_friendlyName,
		.minsize = -1,
		.maxsize = -1,
		.mask = B_ASN1_BMPSTRING,
		.flags = STABLE_NO_MASK,
	},
	{
		.nid = NID_name,
		.minsize = 1,
		.maxsize = ub_name,
		.mask = DIRSTRING_TYPE,
		.flags = 0,
	},
	{
		.nid = NID_dnQualifier,
		.minsize = -1,
		.maxsize = -1,
		.mask = B_ASN1_PRINTABLESTRING,
		.flags = STABLE_NO_MASK,
	},
	{
		.nid = NID_domainComponent,
		.minsize = 1,
		.maxsize = -1,
		.mask = B_ASN1_IA5STRING,
		.flags = STABLE_NO_MASK,
	},
	{
		.nid = NID_ms_csp_name,
		.minsize = -1,
		.maxsize = -1,
		.mask = B_ASN1_BMPSTRING,
		.flags = STABLE_NO_MASK,
	},
};

#define N_STRING_TABLE_ENTRIES (sizeof(tbl_standard) / sizeof(tbl_standard[0]))

const ASN1_STRING_TABLE *
ASN1_STRING_TABLE_get(int nid)
{
	size_t i;

	for (i = 0; i < N_STRING_TABLE_ENTRIES; i++) {
		const ASN1_STRING_TABLE *entry = &tbl_standard[i];
		if (entry->nid == nid)
			return entry;
	}

	return NULL;
}
LCRYPTO_ALIAS(ASN1_STRING_TABLE_get);
