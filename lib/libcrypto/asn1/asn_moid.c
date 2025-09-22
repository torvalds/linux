/* $OpenBSD: asn_moid.c,v 1.20 2025/05/10 11:51:01 tb Exp $ */
/* Written by Stephen Henson (steve@openssl.org) for the OpenSSL
 * project 2001.
 */
/* ====================================================================
 * Copyright (c) 2001-2004 The OpenSSL Project.  All rights reserved.
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

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <openssl/conf.h>
#include <openssl/crypto.h>
#include <openssl/x509.h>

#include "asn1_local.h"
#include "conf_local.h"
#include "err_local.h"

/* Simple ASN1 OID module: add all objects in a given section */

static int do_create(char *value, char *name);

static int
oid_module_init(CONF_IMODULE *md, const CONF *cnf)
{
	int i;
	const char *oid_section;
	STACK_OF(CONF_VALUE) *sktmp;
	CONF_VALUE *oval;

	oid_section = CONF_imodule_get_value(md);
	if (!(sktmp = NCONF_get_section(cnf, oid_section))) {
		ASN1error(ASN1_R_ERROR_LOADING_SECTION);
		return 0;
	}
	for (i = 0; i < sk_CONF_VALUE_num(sktmp); i++) {
		oval = sk_CONF_VALUE_value(sktmp, i);
		if (!do_create(oval->value, oval->name)) {
			ASN1error(ASN1_R_ADDING_OBJECT);
			return 0;
		}
	}
	return 1;
}

static void
oid_module_finish(CONF_IMODULE *md)
{
	OBJ_cleanup();
}

void
ASN1_add_oid_module(void)
{
	CONF_module_add("oid_section", oid_module_init, oid_module_finish);
}

/* Create an OID based on a name value pair. Accept two formats.
 * shortname = 1.2.3.4
 * shortname = some long name, 1.2.3.4
 */

static int
do_create(char *value, char *name)
{
	int nid;
	ASN1_OBJECT *oid;
	char *ln, *ostr, *p, *lntmp;

	p = strrchr(value, ',');
	if (!p) {
		ln = name;
		ostr = value;
	} else {
		ln = NULL;
		ostr = p + 1;
		if (!*ostr)
			return 0;
		while (isspace((unsigned char)*ostr))
			ostr++;
	}

	nid = OBJ_create(ostr, name, ln);

	if (nid == NID_undef)
		return 0;

	if (p) {
		ln = value;
		while (isspace((unsigned char)*ln))
			ln++;
		p--;
		while (isspace((unsigned char)*p)) {
			if (p == ln)
				return 0;
			p--;
		}
		p++;
		lntmp = malloc((p - ln) + 1);
		if (lntmp == NULL)
			return 0;
		memcpy(lntmp, ln, p - ln);
		lntmp[p - ln] = 0;
		oid = OBJ_nid2obj(nid);
		oid->ln = lntmp;
	}

	return 1;
}
