/* $OpenBSD: cert.c,v 1.33 2013/03/21 04:30:14 deraadt Exp $	 */
/* $EOM: cert.c,v 1.18 2000/09/28 12:53:27 niklas Exp $	 */

/*
 * Copyright (c) 1998, 1999 Niels Provos.  All rights reserved.
 * Copyright (c) 1999, 2000 Niklas Hallqvist.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "isakmp_num.h"
#include "log.h"
#include "cert.h"
#include "x509.h"

#include "policy.h"

struct cert_handler cert_handler[] = {
    {
	ISAKMP_CERTENC_X509_SIG,
	x509_cert_init, x509_crl_init, x509_cert_get, x509_cert_validate,
	x509_cert_insert, x509_cert_free,
	x509_certreq_validate, x509_certreq_decode, x509_free_aca,
	x509_cert_obtain, x509_cert_get_key, x509_cert_get_subjects,
	x509_cert_dup, x509_serialize, x509_printable, x509_from_printable,
	x509_ca_count
    },
    {
	ISAKMP_CERTENC_KEYNOTE,
	keynote_cert_init, NULL, keynote_cert_get, keynote_cert_validate,
	keynote_cert_insert, keynote_cert_free,
	keynote_certreq_validate, keynote_certreq_decode, keynote_free_aca,
	keynote_cert_obtain, keynote_cert_get_key, keynote_cert_get_subjects,
	keynote_cert_dup, keynote_serialize, keynote_printable,
	keynote_from_printable, keynote_ca_count
    },
};

/* Initialize all certificate handlers */
int
cert_init(void)
{
	size_t	i;
	int	err = 1;

	for (i = 0; i < sizeof cert_handler / sizeof cert_handler[0]; i++)
		if (cert_handler[i].cert_init &&
		    !(*cert_handler[i].cert_init)())
			err = 0;

	return err;
}

int
crl_init(void)
{
	size_t	i;
	int	err = 1;

	for (i = 0; i < sizeof cert_handler / sizeof cert_handler[0]; i++)
		if (cert_handler[i].crl_init && !(*cert_handler[i].crl_init)())
			err = 0;

	return err;
}

struct cert_handler *
cert_get(u_int16_t id)
{
	size_t	i;

	for (i = 0; i < sizeof cert_handler / sizeof cert_handler[0]; i++)
		if (id == cert_handler[i].id)
			return &cert_handler[i];
	return 0;
}

/*
 * Decode the certificate request of type TYPE contained in DATA extending
 * DATALEN bytes.  Return a certreq_aca structure which the caller is
 * responsible for deallocating.
 */
struct certreq_aca *
certreq_decode(u_int16_t type, u_int8_t *data, u_int32_t datalen)
{
	struct cert_handler *handler;
	struct certreq_aca aca, *ret;

	handler = cert_get(type);
	if (!handler)
		return 0;

	aca.id = type;
	aca.handler = handler;
	aca.data = aca.raw_ca = NULL;

	if (datalen > 0) {
		int rc;

		rc = handler->certreq_decode(&aca.data, data, datalen);
		if (!rc)
			return 0;

		aca.raw_ca = malloc(datalen);
		if (aca.raw_ca == NULL) {
			log_error("certreq_decode: malloc (%lu) failed",
			    (unsigned long)datalen);
			handler->free_aca(aca.data);
			return 0;
		}

		memcpy(aca.raw_ca, data, datalen);
	}
	aca.raw_ca_len = datalen;

	ret = malloc(sizeof aca);
	if (!ret) {
		log_error("certreq_decode: malloc (%lu) failed",
		    (unsigned long)sizeof aca);
		free(aca.raw_ca);
		handler->free_aca(aca.data);
		return 0;
	}
	memcpy(ret, &aca, sizeof aca);
	return ret;
}

void
cert_free_subjects(int n, u_int8_t **id, u_int32_t *len)
{
	int	i;

	for (i = 0; i < n; i++)
		free(id[i]);
	free(id);
	free(len);
}
