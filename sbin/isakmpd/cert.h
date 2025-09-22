/* $OpenBSD: cert.h,v 1.16 2015/01/16 06:39:58 deraadt Exp $	 */
/* $EOM: cert.h,v 1.8 2000/09/28 12:53:27 niklas Exp $	 */

/*
 * Copyright (c) 1998, 1999 Niels Provos.  All rights reserved.
 * Copyright (c) 2000, 2001 Niklas Hallqvist.  All rights reserved.
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

#ifndef _CERT_H_
#define _CERT_H_

#include <sys/types.h>
#include <sys/queue.h>

/*
 * CERT handler for each kind of certificate:
 *
 * cert_init - initialize CERT handler.
 * crl_init - initialize CRLs, if applicable.
 * cert_get - get a certificate in internal representation from raw data.
 * cert_validate - validated a certificate, if it returns != 0 we can use it.
 * cert_insert - inserts cert into memory storage, we can retrieve with
 *               cert_obtain.
 * cert_dup - duplicate a certificate
 * cert_serialize - convert to a "serialized" form; KeyNote stays the same,
 *                  X509 is converted to the ASN1 notation.
 * cert_printable - for X509, the hex representation of the serialized form;
 *                  for KeyNote, itself.
 * cert_from_printable - the reverse of cert_printable
 * ca_count - how many CAs we have in our store (for CERT_REQ processing)
 */

struct cert_handler {
	u_int16_t id;	/* ISAKMP Cert Encoding ID */
	int	 (*cert_init)(void);
	int	 (*crl_init)(void);
	void	*(*cert_get)(u_int8_t *, u_int32_t);
	int	 (*cert_validate)(void *);
	int	 (*cert_insert)(int, void *);
	void	 (*cert_free)(void *);
	int	 (*certreq_validate)(u_int8_t *, u_int32_t);
	int	 (*certreq_decode)(void **, u_int8_t *, u_int32_t);
	void	 (*free_aca)(void *);
	int	 (*cert_obtain)(u_int8_t *, size_t, void *, u_int8_t **,
		     u_int32_t *);
	int	 (*cert_get_key) (void *, void *);
	int	 (*cert_get_subjects) (void *, int *, u_int8_t ***,
		     u_int32_t **);
	void	*(*cert_dup) (void *);
	void	 (*cert_serialize) (void *, u_int8_t **, u_int32_t *);
	char	*(*cert_printable) (void *);
	void	*(*cert_from_printable) (char *);
	int	 (*ca_count)(void);
};

/* The acceptable authority of cert request.  */
struct certreq_aca {
	TAILQ_ENTRY(certreq_aca) link;

	u_int16_t id;
	struct cert_handler *handler;

	/* If data is a null pointer, everything is acceptable.  */
	void	*data;

	/* Copy of raw CA value received */
	u_int32_t raw_ca_len;
	void	*raw_ca;
};

struct certreq_aca *certreq_decode(u_int16_t, u_int8_t *, u_int32_t);
void	cert_free_subjects(int, u_int8_t **, u_int32_t *);
struct cert_handler *cert_get(u_int16_t);
int	cert_init(void);
int	crl_init(void);

#endif				/* _CERT_H_ */
