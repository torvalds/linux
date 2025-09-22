/* $OpenBSD: x509.c,v 1.126 2024/04/28 16:43:42 florian Exp $	 */
/* $EOM: x509.c,v 1.54 2001/01/16 18:42:16 ho Exp $	 */

/*
 * Copyright (c) 1998, 1999 Niels Provos.  All rights reserved.
 * Copyright (c) 1999, 2000, 2001 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 1999, 2000, 2001 Angelos D. Keromytis.  All rights reserved.
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
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include <regex.h>
#include <keynote.h>

#include "cert.h"
#include "conf.h"
#include "exchange.h"
#include "hash.h"
#include "ike_auth.h"
#include "ipsec.h"
#include "log.h"
#include "dh.h"
#include "monitor.h"
#include "policy.h"
#include "sa.h"
#include "util.h"
#include "x509.h"

static u_int16_t x509_hash(u_int8_t *, size_t);
static void	 x509_hash_init(void);
static X509	*x509_hash_find(u_int8_t *, size_t);
static int	 x509_hash_enter(X509 *);

/*
 * X509_STOREs do not support subjectAltNames, so we have to build
 * our own hash table.
 */

/*
 * XXX Actually this store is not really useful, we never use it as we have
 * our own hash table.  It also gets collisions if we have several certificates
 * only differing in subjectAltName.
 */
static X509_STORE *x509_certs = 0;
static X509_STORE *x509_cas = 0;

static int n_x509_cas = 0;

/* Initial number of bits used as hash.  */
#define INITIAL_BUCKET_BITS 6

struct x509_hash {
	LIST_ENTRY(x509_hash) link;

	X509		*cert;
};

static LIST_HEAD(x509_list, x509_hash) *x509_tab = 0;

/* Works both as a maximum index and a mask.  */
static int	bucket_mask;

/*
 * Given an X509 certificate, create a KeyNote assertion where
 * Issuer/Subject -> Authorizer/Licensees.
 * XXX RSA-specific.
 */
int
x509_generate_kn(int id, X509 *cert)
{
	static const char fmt[] = "Authorizer: \"rsa-hex:%s\"\nLicensees: \"rsa-hex:%s"
		    "\"\nConditions: %s >= \"%s\" && %s <= \"%s\";\n";
	char	*ikey = NULL, *skey = NULL, *buf = NULL;
	char	isname[256], subname[256];
	static const char fmt2[] = "Authorizer: \"DN:%s\"\nLicensees: \"DN:%s\"\n"
		    "Conditions: %s >= \"%s\" && %s <= \"%s\";\n";
	X509_NAME *issuer, *subject;
	struct keynote_deckey dc;
	X509_STORE_CTX *csc = NULL;
	X509_OBJECT *obj = NULL;
	X509	*icert;
	RSA	*key = NULL;
	time_t	tt;
	char	before[15], after[15], *timecomp, *timecomp2;
	ASN1_TIME *tm;
	int	i;

	LOG_DBG((LOG_POLICY, 90,
	    "x509_generate_kn: generating KeyNote policy for certificate %p",
	    cert));

	issuer = X509_get_issuer_name(cert);
	subject = X509_get_subject_name(cert);

	/* Missing or self-signed, ignore cert but don't report failure.  */
	if (!issuer || !subject || !X509_NAME_cmp(issuer, subject))
		return 1;

	if (!x509_cert_get_key(cert, &key)) {
		LOG_DBG((LOG_POLICY, 30,
		    "x509_generate_kn: failed to get public key from cert"));
		return 0;
	}
	dc.dec_algorithm = KEYNOTE_ALGORITHM_RSA;
	dc.dec_key = key;
	ikey = kn_encode_key(&dc, INTERNAL_ENC_PKCS1, ENCODING_HEX,
	    KEYNOTE_PUBLIC_KEY);
	if (keynote_errno == ERROR_MEMORY) {
		log_print("x509_generate_kn: failed to get memory for "
		    "public key");
		LOG_DBG((LOG_POLICY, 30, "x509_generate_kn: cannot get "
		    "subject key"));
		goto fail;
	}
	if (!ikey) {
		LOG_DBG((LOG_POLICY, 30, "x509_generate_kn: cannot get "
		    "subject key"));
		goto fail;
	}

	RSA_free(key);
	key = NULL;

	csc = X509_STORE_CTX_new();
	if (csc == NULL) {
		log_print("x509_generate_kn: failed to get memory for "
		    "certificate store");
		goto fail;
	}
	obj = X509_OBJECT_new();
	if (obj == NULL) {
		log_print("x509_generate_kn: failed to get memory for "
		    "certificate object");
		goto fail;
	}

	/* Now find issuer's certificate so we can get the public key.  */
	X509_STORE_CTX_init(csc, x509_cas, cert, NULL);
	if (X509_STORE_get_by_subject(csc, X509_LU_X509, issuer, obj) !=
	    X509_LU_X509) {
		X509_STORE_CTX_cleanup(csc);
		X509_STORE_CTX_init(csc, x509_certs, cert, NULL);
		if (X509_STORE_get_by_subject(csc, X509_LU_X509, issuer, obj)
		    != X509_LU_X509) {
			X509_STORE_CTX_cleanup(csc);
			LOG_DBG((LOG_POLICY, 30,
			    "x509_generate_kn: no certificate found for "
			    "issuer"));
			goto fail;
		}
	}
	X509_STORE_CTX_free(csc);
	csc = NULL;

	icert = X509_OBJECT_get0_X509(obj);
	if (icert == NULL) {
		LOG_DBG((LOG_POLICY, 30, "x509_generate_kn: "
		    "missing certificates, cannot construct X509 chain"));
		goto fail;
	}
	if (!x509_cert_get_key(icert, &key)) {
		LOG_DBG((LOG_POLICY, 30,
		    "x509_generate_kn: failed to get public key from cert"));
		goto fail;
	}
	X509_OBJECT_free(obj);
	obj = NULL;

	dc.dec_algorithm = KEYNOTE_ALGORITHM_RSA;
	dc.dec_key = key;
	skey = kn_encode_key(&dc, INTERNAL_ENC_PKCS1, ENCODING_HEX,
	    KEYNOTE_PUBLIC_KEY);
	if (keynote_errno == ERROR_MEMORY) {
		log_error("x509_generate_kn: failed to get memory for public "
		    "key");
		LOG_DBG((LOG_POLICY, 30, "x509_generate_kn: cannot get issuer "
		    "key"));
		goto fail;
	}
	if (!skey) {
		LOG_DBG((LOG_POLICY, 30, "x509_generate_kn: cannot get issuer "
		    "key"));
		goto fail;
	}

	RSA_free(key);
	key = NULL;

	if (((tm = X509_get_notBefore(cert)) == NULL) ||
	    (tm->type != V_ASN1_UTCTIME &&
		tm->type != V_ASN1_GENERALIZEDTIME)) {
		struct tm *ltm;

		tt = time(NULL);
		if ((ltm = localtime(&tt)) == NULL) {
			LOG_DBG((LOG_POLICY, 30,
			    "x509_generate_kn: invalid local time"));
			goto fail;
		}
		strftime(before, 14, "%Y%m%d%H%M%S", ltm);
		timecomp = "LocalTimeOfDay";
	} else {
		if (tm->data[tm->length - 1] == 'Z') {
			timecomp = "GMTTimeOfDay";
			i = tm->length - 2;
		} else {
			timecomp = "LocalTimeOfDay";
			i = tm->length - 1;
		}

		for (; i >= 0; i--) {
			if (tm->data[i] < '0' || tm->data[i] > '9') {
				LOG_DBG((LOG_POLICY, 30,
				    "x509_generate_kn: invalid data in "
				    "NotValidBefore time field"));
				goto fail;
			}
		}

		if (tm->type == V_ASN1_UTCTIME) {
			if ((tm->length < 10) || (tm->length > 13)) {
				LOG_DBG((LOG_POLICY, 30,
				    "x509_generate_kn: invalid length "
				    "of NotValidBefore time field (%d)",
				    tm->length));
				goto fail;
			}
			/* Validity checks.  */
			if ((tm->data[2] != '0' && tm->data[2] != '1') ||
			    (tm->data[2] == '0' && tm->data[3] == '0') ||
			    (tm->data[2] == '1' && tm->data[3] > '2') ||
			    (tm->data[4] > '3') ||
			    (tm->data[4] == '0' && tm->data[5] == '0') ||
			    (tm->data[4] == '3' && tm->data[5] > '1') ||
			    (tm->data[6] > '2') ||
			    (tm->data[6] == '2' && tm->data[7] > '3') ||
			    (tm->data[8] > '5')) {
				LOG_DBG((LOG_POLICY, 30,
				    "x509_generate_kn: invalid value in "
				    "NotValidBefore time field"));
				goto fail;
			}
			/* Stupid UTC tricks.  */
			if (tm->data[0] < '5')
				snprintf(before, sizeof before, "20%s",
				    tm->data);
			else
				snprintf(before, sizeof before, "19%s",
				    tm->data);
		} else {	/* V_ASN1_GENERICTIME */
			if ((tm->length < 12) || (tm->length > 15)) {
				LOG_DBG((LOG_POLICY, 30,
				    "x509_generate_kn: invalid length of "
				    "NotValidBefore time field (%d)",
				    tm->length));
				goto fail;
			}
			/* Validity checks.  */
			if ((tm->data[4] != '0' && tm->data[4] != '1') ||
			    (tm->data[4] == '0' && tm->data[5] == '0') ||
			    (tm->data[4] == '1' && tm->data[5] > '2') ||
			    (tm->data[6] > '3') ||
			    (tm->data[6] == '0' && tm->data[7] == '0') ||
			    (tm->data[6] == '3' && tm->data[7] > '1') ||
			    (tm->data[8] > '2') ||
			    (tm->data[8] == '2' && tm->data[9] > '3') ||
			    (tm->data[10] > '5')) {
				LOG_DBG((LOG_POLICY, 30,
				    "x509_generate_kn: invalid value in "
				    "NotValidBefore time field"));
				goto fail;
			}
			snprintf(before, sizeof before, "%s", tm->data);
		}

		/* Fix missing seconds.  */
		if (tm->length < 12) {
			before[12] = '0';
			before[13] = '0';
		}
		/* This will overwrite trailing 'Z'.  */
		before[14] = '\0';
	}

	tm = X509_get_notAfter(cert);
	if (tm == NULL ||
	    (tm->type != V_ASN1_UTCTIME &&
		tm->type != V_ASN1_GENERALIZEDTIME)) {
		struct tm *ltm;

		tt = time(0);
		if ((ltm = localtime(&tt)) == NULL) {
			LOG_DBG((LOG_POLICY, 30,
			    "x509_generate_kn: invalid local time"));
			goto fail;
		}
		strftime(after, 14, "%Y%m%d%H%M%S", ltm);
		timecomp2 = "LocalTimeOfDay";
	} else {
		if (tm->data[tm->length - 1] == 'Z') {
			timecomp2 = "GMTTimeOfDay";
			i = tm->length - 2;
		} else {
			timecomp2 = "LocalTimeOfDay";
			i = tm->length - 1;
		}

		for (; i >= 0; i--) {
			if (tm->data[i] < '0' || tm->data[i] > '9') {
				LOG_DBG((LOG_POLICY, 30,
				    "x509_generate_kn: invalid data in "
				    "NotValidAfter time field"));
				goto fail;
			}
		}

		if (tm->type == V_ASN1_UTCTIME) {
			if ((tm->length < 10) || (tm->length > 13)) {
				LOG_DBG((LOG_POLICY, 30,
				    "x509_generate_kn: invalid length of "
				    "NotValidAfter time field (%d)",
				    tm->length));
				goto fail;
			}
			/* Validity checks. */
			if ((tm->data[2] != '0' && tm->data[2] != '1') ||
			    (tm->data[2] == '0' && tm->data[3] == '0') ||
			    (tm->data[2] == '1' && tm->data[3] > '2') ||
			    (tm->data[4] > '3') ||
			    (tm->data[4] == '0' && tm->data[5] == '0') ||
			    (tm->data[4] == '3' && tm->data[5] > '1') ||
			    (tm->data[6] > '2') ||
			    (tm->data[6] == '2' && tm->data[7] > '3') ||
			    (tm->data[8] > '5')) {
				LOG_DBG((LOG_POLICY, 30,
				    "x509_generate_kn: invalid value in "
				    "NotValidAfter time field"));
				goto fail;
			}
			/* Stupid UTC tricks.  */
			if (tm->data[0] < '5')
				snprintf(after, sizeof after, "20%s",
				    tm->data);
			else
				snprintf(after, sizeof after, "19%s",
				    tm->data);
		} else {	/* V_ASN1_GENERICTIME */
			if ((tm->length < 12) || (tm->length > 15)) {
				LOG_DBG((LOG_POLICY, 30,
				    "x509_generate_kn: invalid length of "
				    "NotValidAfter time field (%d)",
				    tm->length));
				goto fail;
			}
			/* Validity checks.  */
			if ((tm->data[4] != '0' && tm->data[4] != '1') ||
			    (tm->data[4] == '0' && tm->data[5] == '0') ||
			    (tm->data[4] == '1' && tm->data[5] > '2') ||
			    (tm->data[6] > '3') ||
			    (tm->data[6] == '0' && tm->data[7] == '0') ||
			    (tm->data[6] == '3' && tm->data[7] > '1') ||
			    (tm->data[8] > '2') ||
			    (tm->data[8] == '2' && tm->data[9] > '3') ||
			    (tm->data[10] > '5')) {
				LOG_DBG((LOG_POLICY, 30,
				    "x509_generate_kn: invalid value in "
				    "NotValidAfter time field"));
				goto fail;
			}
			snprintf(after, sizeof after, "%s", tm->data);
		}

		/* Fix missing seconds.  */
		if (tm->length < 12) {
			after[12] = '0';
			after[13] = '0';
		}
		after[14] = '\0';	/* This will overwrite trailing 'Z' */
	}

	if (asprintf(&buf, fmt, skey, ikey, timecomp, before, timecomp2,
	    after) == -1) {
		log_error("x509_generate_kn: "
		    "failed to allocate memory for KeyNote credential");
		goto fail;
	}
	
	free(ikey);
	ikey = NULL;
	free(skey);
	skey = NULL;

	if (kn_add_assertion(id, buf, strlen(buf), ASSERT_FLAG_LOCAL) == -1) {
		LOG_DBG((LOG_POLICY, 30,
		    "x509_generate_kn: failed to add new KeyNote credential"));
		goto fail;
	}
	/* We could print the assertion here, but log_print() truncates...  */
	LOG_DBG((LOG_POLICY, 60, "x509_generate_kn: added credential"));

	free(buf);
	buf = NULL;

	if (!X509_NAME_oneline(issuer, isname, 256)) {
		LOG_DBG((LOG_POLICY, 50,
		    "x509_generate_kn: "
		    "X509_NAME_oneline (issuer, ...) failed"));
		goto fail;
	}
	if (!X509_NAME_oneline(subject, subname, 256)) {
		LOG_DBG((LOG_POLICY, 50,
		    "x509_generate_kn: "
		    "X509_NAME_oneline (subject, ...) failed"));
		goto fail;
	}
	if (asprintf(&buf, fmt2, isname, subname, timecomp, before,
	    timecomp2, after) == -1) {
		log_error("x509_generate_kn: malloc failed");
		return 0;
	}

	if (kn_add_assertion(id, buf, strlen(buf), ASSERT_FLAG_LOCAL) == -1) {
		LOG_DBG((LOG_POLICY, 30,
		    "x509_generate_kn: failed to add new KeyNote credential"));
		goto fail;
	}
	LOG_DBG((LOG_POLICY, 80, "x509_generate_kn: added credential:\n%s",
	    buf));

	free(buf);
	return 1;

fail:
	X509_STORE_CTX_free(csc);
	X509_OBJECT_free(obj);
	free(buf);
	free(skey);
	free(ikey);
	if (key)
		RSA_free(key);

	return 0;
}

static u_int16_t
x509_hash(u_int8_t *id, size_t len)
{
	u_int16_t bucket = 0;
	size_t	i;

	/* XXX We might resize if we are crossing a certain threshold.  */
	for (i = 4; i < (len & ~1); i += 2) {
		/* Doing it this way avoids alignment problems.  */
		bucket ^= (id[i] + 1) * (id[i + 1] + 257);
	}
	/* Hash in the last character of odd length IDs too.  */
	if (i < len)
		bucket ^= (id[i] + 1) * (id[i] + 257);

	bucket &= bucket_mask;
	return bucket;
}

static void
x509_hash_init(void)
{
	struct x509_hash *certh;
	int	i;

	bucket_mask = (1 << INITIAL_BUCKET_BITS) - 1;

	/* If reinitializing, free existing entries.  */
	if (x509_tab) {
		for (i = 0; i <= bucket_mask; i++)
			for (certh = LIST_FIRST(&x509_tab[i]); certh;
			    certh = LIST_FIRST(&x509_tab[i])) {
				LIST_REMOVE(certh, link);
				free(certh);
			}
		free(x509_tab);
	}
	x509_tab = calloc(bucket_mask + 1, sizeof(struct x509_list));
	if (!x509_tab)
		log_fatal("x509_hash_init: malloc (%lu) failed",
		    (bucket_mask + 1) *
		    (unsigned long)sizeof(struct x509_list));
	for (i = 0; i <= bucket_mask; i++) {
		LIST_INIT(&x509_tab[i]);
	}
}

/* Lookup a certificate by an ID blob.  */
static X509 *
x509_hash_find(u_int8_t *id, size_t len)
{
	struct x509_hash *cert;
	u_int8_t	**cid;
	u_int32_t	*clen;
	int	n, i, id_found;

	for (cert = LIST_FIRST(&x509_tab[x509_hash(id, len)]); cert;
	    cert = LIST_NEXT(cert, link)) {
		if (!x509_cert_get_subjects(cert->cert, &n, &cid, &clen))
			continue;

		id_found = 0;
		for (i = 0; i < n; i++) {
			LOG_DBG_BUF((LOG_CRYPTO, 70, "cert_cmp", id, len));
			LOG_DBG_BUF((LOG_CRYPTO, 70, "cert_cmp", cid[i],
			    clen[i]));
			/*
			 * XXX This identity predicate needs to be
			 * understood.
			 */
			if (clen[i] == len && id[0] == cid[i][0] &&
			    memcmp(id + 4, cid[i] + 4, len - 4) == 0) {
				id_found++;
				break;
			}
		}
		cert_free_subjects(n, cid, clen);
		if (!id_found)
			continue;

		LOG_DBG((LOG_CRYPTO, 70, "x509_hash_find: return X509 %p",
		    cert->cert));
		return cert->cert;
	}

	LOG_DBG((LOG_CRYPTO, 70,
	    "x509_hash_find: no certificate matched query"));
	return 0;
}

static int
x509_hash_enter(X509 *cert)
{
	u_int16_t	bucket = 0;
	u_int8_t	**id;
	u_int32_t	*len;
	struct x509_hash *certh;
	int	n, i;

	if (!x509_cert_get_subjects(cert, &n, &id, &len)) {
		log_print("x509_hash_enter: cannot retrieve subjects");
		return 0;
	}
	for (i = 0; i < n; i++) {
		certh = calloc(1, sizeof *certh);
		if (!certh) {
			cert_free_subjects(n, id, len);
			log_error("x509_hash_enter: calloc (1, %lu) failed",
			    (unsigned long)sizeof *certh);
			return 0;
		}
		certh->cert = cert;

		bucket = x509_hash(id[i], len[i]);

		LIST_INSERT_HEAD(&x509_tab[bucket], certh, link);
		LOG_DBG((LOG_CRYPTO, 70,
		    "x509_hash_enter: cert %p added to bucket %d",
		    cert, bucket));
	}
	cert_free_subjects(n, id, len);

	return 1;
}

/* X509 Certificate Handling functions.  */

int
x509_read_from_dir(X509_STORE *ctx, char *name, int hash, int *pcount)
{
	FILE		*certfp;
	X509		*cert;
	struct stat	sb;
	char		fullname[PATH_MAX];
	char		file[PATH_MAX];
	int		fd;

	if (strlen(name) >= sizeof fullname - 1) {
		log_print("x509_read_from_dir: directory name too long");
		return 0;
	}
	LOG_DBG((LOG_CRYPTO, 40, "x509_read_from_dir: reading certs from %s",
	    name));

	if (monitor_req_readdir(name) == -1) {
		LOG_DBG((LOG_CRYPTO, 10,
		    "x509_read_from_dir: opendir (\"%s\") failed: %s",
		    name, strerror(errno)));
		return 0;
	}

	while ((fd = monitor_readdir(file, sizeof file)) != -1) {
		LOG_DBG((LOG_CRYPTO, 60,
		    "x509_read_from_dir: reading certificate %s",
		    file));

		if (fstat(fd, &sb) == -1) {
			log_error("x509_read_from_dir: fstat failed");
			close(fd);
			continue;
		}

		if (!S_ISREG(sb.st_mode)) {
			close(fd);
			continue;
		}

		if ((certfp = fdopen(fd, "r")) == NULL) {
			log_error("x509_read_from_dir: fdopen failed");
			close(fd);
			continue;
		}

#if SSLEAY_VERSION_NUMBER >= 0x00904100L
		cert = PEM_read_X509(certfp, NULL, NULL, NULL);
#else
		cert = PEM_read_X509(certfp, NULL, NULL);
#endif
		fclose(certfp);

		if (cert == NULL) {
			log_print("x509_read_from_dir: PEM_read_X509 "
			    "failed for %s", file);
			continue;
		}

		if (pcount != NULL)
			(*pcount)++;

		if (!X509_STORE_add_cert(ctx, cert)) {
			/*
			 * This is actually expected if we have several
			 * certificates only differing in subjectAltName,
			 * which is not an something that is strange.
			 * Consider multi-homed machines.
			*/
			LOG_DBG((LOG_CRYPTO, 50,
			    "x509_read_from_dir: X509_STORE_add_cert failed "
			    "for %s", file));
		}
		if (hash)
			if (!x509_hash_enter(cert))
				log_print("x509_read_from_dir: "
				    "x509_hash_enter (%s) failed",
				    file);
	}

	return 1;
}

/* XXX share code with x509_read_from_dir() ?  */
int
x509_read_crls_from_dir(X509_STORE *ctx, char *name)
{
	FILE		*crlfp;
	X509_CRL	*crl;
	struct stat	sb;
	char		fullname[PATH_MAX];
	char		file[PATH_MAX];
	int		fd;

	if (strlen(name) >= sizeof fullname - 1) {
		log_print("x509_read_crls_from_dir: directory name too long");
		return 0;
	}
	LOG_DBG((LOG_CRYPTO, 40, "x509_read_crls_from_dir: reading CRLs "
	    "from %s", name));

	if (monitor_req_readdir(name) == -1) {
		LOG_DBG((LOG_CRYPTO, 10, "x509_read_crls_from_dir: opendir "
		    "(\"%s\") failed: %s", name, strerror(errno)));
		return 0;
	}
	strlcpy(fullname, name, sizeof fullname);

	while ((fd = monitor_readdir(file, sizeof file)) != -1) {
		LOG_DBG((LOG_CRYPTO, 60, "x509_read_crls_from_dir: reading "
		    "CRL %s", file));

		if (fstat(fd, &sb) == -1) {
			log_error("x509_read_crls_from_dir: fstat failed");
			close(fd);
			continue;
		}

		if (!S_ISREG(sb.st_mode)) {
			close(fd);
			continue;
		}

		if ((crlfp = fdopen(fd, "r")) == NULL) {
			log_error("x509_read_crls_from_dir: fdopen failed");
			close(fd);
			continue;
		}

		crl = PEM_read_X509_CRL(crlfp, NULL, NULL, NULL);

		fclose(crlfp);

		if (crl == NULL) {
			log_print("x509_read_crls_from_dir: "
			    "PEM_read_X509_CRL failed for %s",
			    file);
			continue;
		}
		if (!X509_STORE_add_crl(ctx, crl)) {
			LOG_DBG((LOG_CRYPTO, 50, "x509_read_crls_from_dir: "
			    "X509_STORE_add_crl failed for %s", file));
			continue;
		}
		/*
		 * XXX This is to make x509_cert_validate set this (and
		 * XXX another) flag when validating certificates. Currently,
		 * XXX OpenSSL defaults to reject an otherwise valid
		 * XXX certificate (chain) if these flags are set but there
		 * XXX are no CRLs to check. The current workaround is to only
		 * XXX set the flags if we actually loaded some CRL data.
		 */
		X509_STORE_set_flags(ctx, X509_V_FLAG_CRL_CHECK);
	}

	return 1;
}

/* Initialize our databases and load our own certificates.  */
int
x509_cert_init(void)
{
	char	*dirname;

	x509_hash_init();

	/* Process CA certificates we will trust.  */
	dirname = conf_get_str("X509-certificates", "CA-directory");
	if (!dirname) {
		log_print("x509_cert_init: no CA-directory");
		return 0;
	}
	/* Free if already initialized.  */
	if (x509_cas)
		X509_STORE_free(x509_cas);

	x509_cas = X509_STORE_new();
	if (!x509_cas) {
		log_print("x509_cert_init: creating new X509_STORE failed");
		return 0;
	}
	if (!x509_read_from_dir(x509_cas, dirname, 0, &n_x509_cas)) {
		log_print("x509_cert_init: x509_read_from_dir failed");
		return 0;
	}
	/* Process client certificates we will accept.  */
	dirname = conf_get_str("X509-certificates", "Cert-directory");
	if (!dirname) {
		log_print("x509_cert_init: no Cert-directory");
		return 0;
	}
	/* Free if already initialized.  */
	if (x509_certs)
		X509_STORE_free(x509_certs);

	x509_certs = X509_STORE_new();
	if (!x509_certs) {
		log_print("x509_cert_init: creating new X509_STORE failed");
		return 0;
	}
	if (!x509_read_from_dir(x509_certs, dirname, 1, NULL)) {
		log_print("x509_cert_init: x509_read_from_dir failed");
		return 0;
	}
	return 1;
}

int
x509_crl_init(void)
{
	/*
	 * XXX I'm not sure if the method to use CRLs in certificate validation
	 * is valid for OpenSSL versions prior to 0.9.7. For now, simply do not
	 * support it.
	 */
	char	*dirname;
	dirname = conf_get_str("X509-certificates", "CRL-directory");
	if (!dirname) {
		log_print("x509_crl_init: no CRL-directory");
		return 0;
	}
	if (!x509_read_crls_from_dir(x509_cas, dirname)) {
		LOG_DBG((LOG_MISC, 10,
		    "x509_crl_init: x509_read_crls_from_dir failed"));
		return 0;
	}

	return 1;
}

void *
x509_cert_get(u_int8_t *asn, u_int32_t len)
{
	return x509_from_asn(asn, len);
}

int
x509_cert_validate(void *scert)
{
	X509_STORE_CTX	*csc;
	X509_NAME	*issuer, *subject;
	X509		*cert = (X509 *) scert;
	EVP_PKEY	*key;
	int		res, err, flags;

	/*
	 * Validate the peer certificate by checking with the CA certificates
	 * we trust.
	 */
	csc = X509_STORE_CTX_new();
	if (csc == NULL) {
		log_print("x509_cert_validate: failed to get memory for "
		    "certificate store");
		return 0;
	}
	X509_STORE_CTX_init(csc, x509_cas, cert, NULL);
	/* XXX See comment in x509_read_crls_from_dir.  */
	flags = X509_VERIFY_PARAM_get_flags(X509_STORE_get0_param(x509_cas));
	if (flags & X509_V_FLAG_CRL_CHECK) {
		X509_STORE_CTX_set_flags(csc, X509_V_FLAG_CRL_CHECK);
		X509_STORE_CTX_set_flags(csc, X509_V_FLAG_CRL_CHECK_ALL);
	}
	res = X509_verify_cert(csc);
	err = X509_STORE_CTX_get_error(csc);
	X509_STORE_CTX_free(csc);

	/*
	 * Return if validation succeeded or self-signed certs are not
	 * accepted.
	 *
	 * XXX X509_verify_cert seems to return -1 if the validation should be
	 * retried somehow.  We take this as an error and give up.
	 */
	if (res > 0)
		return 1;
	else if (res < 0 ||
	    (res == 0 && err != X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT)) {
		if (err)
			log_print("x509_cert_validate: %.100s",
			    X509_verify_cert_error_string(err));
		return 0;
	} else if (!conf_get_str("X509-certificates", "Accept-self-signed")) {
		if (err)
			log_print("x509_cert_validate: %.100s",
			    X509_verify_cert_error_string(err));
		return 0;
	}
	issuer = X509_get_issuer_name(cert);
	subject = X509_get_subject_name(cert);

	if (!issuer || !subject || X509_NAME_cmp(issuer, subject))
		return 0;

	key = X509_get_pubkey(cert);
	if (!key) {
		log_print("x509_cert_validate: could not get public key from "
		    "self-signed cert");
		return 0;
	}
	if (X509_verify(cert, key) == -1) {
		log_print("x509_cert_validate: self-signed cert is bad");
		return 0;
	}
	return 1;
}

int
x509_cert_insert(int id, void *scert)
{
	X509	*cert;
	int	 res;

	cert = X509_dup((X509 *)scert);
	if (!cert) {
		log_print("x509_cert_insert: X509_dup failed");
		return 0;
	}
	if (x509_generate_kn(id, cert) == 0) {
		LOG_DBG((LOG_POLICY, 50,
		    "x509_cert_insert: x509_generate_kn failed"));
		X509_free(cert);
		return 0;
	}

	res = x509_hash_enter(cert);
	if (!res)
		X509_free(cert);

	return res;
}

static struct x509_hash *
x509_hash_lookup(X509 *cert)
{
	struct x509_hash *certh;
	int	i;

	for (i = 0; i <= bucket_mask; i++)
		for (certh = LIST_FIRST(&x509_tab[i]); certh;
		    certh = LIST_NEXT(certh, link))
			if (certh->cert == cert)
				return certh;
	return 0;
}

void
x509_cert_free(void *cert)
{
	struct x509_hash *certh = x509_hash_lookup((X509 *) cert);

	if (certh)
		LIST_REMOVE(certh, link);
	X509_free((X509 *) cert);
}

/* Validate the BER Encoding of a RDNSequence in the CERT_REQ payload.  */
int
x509_certreq_validate(u_int8_t *asn, u_int32_t len)
{
	int	res = 1;
#if 0
	struct norm_type name = SEQOF("issuer", RDNSequence);

	if (!asn_template_clone(&name, 1) ||
	    (asn = asn_decode_sequence(asn, len, &name)) == 0) {
		log_print("x509_certreq_validate: can not decode 'acceptable "
		    "CA' info");
		res = 0;
	}
	asn_free(&name);
#endif

	/* XXX - not supported directly in SSL - later.  */

	return res;
}

/* Decode the BER Encoding of a RDNSequence in the CERT_REQ payload.  */
int
x509_certreq_decode(void **pdata, u_int8_t *asn, u_int32_t len)
{
#if 0
	/* XXX This needs to be done later.  */
	struct norm_type aca = SEQOF("aca", RDNSequence);
	struct norm_type *tmp;
	struct x509_aca naca, *ret;

	if (!asn_template_clone(&aca, 1) ||
	    (asn = asn_decode_sequence(asn, len, &aca)) == 0) {
		log_print("x509_certreq_decode: can not decode 'acceptable "
		    "CA' info");
		goto fail;
	}
	bzero(&naca, sizeof(naca));

	tmp = asn_decompose("aca.RelativeDistinguishedName."
	    "AttributeValueAssertion", &aca);
	if (!tmp)
		goto fail;
	x509_get_attribval(tmp, &naca.name1);

	tmp = asn_decompose("aca.RelativeDistinguishedName[1]"
	    ".AttributeValueAssertion", &aca);
	if (tmp)
		x509_get_attribval(tmp, &naca.name2);

	asn_free(&aca);

	ret = malloc(sizeof(struct x509_aca));
	if (ret)
		memcpy(ret, &naca, sizeof(struct x509_aca));
	else {
		log_error("x509_certreq_decode: malloc (%lu) failed",
		    (unsigned long) sizeof(struct x509_aca));
		x509_free_aca(&aca);
	}

	return ret;

fail:
	asn_free(&aca);
#endif
	return 1;
}

void
x509_free_aca(void *blob)
{
	struct x509_aca *aca = blob;

	if (aca != NULL) {
		free(aca->name1.type);
		free(aca->name1.val);

		free(aca->name2.type);
		free(aca->name2.val);
	}
}

X509 *
x509_from_asn(u_char *asn, u_int len)
{
	BIO		*certh;
	X509		*scert = 0;

	certh = BIO_new(BIO_s_mem());
	if (!certh) {
		log_error("x509_from_asn: BIO_new (BIO_s_mem ()) failed");
		return 0;
	}
	if (BIO_write(certh, asn, len) == -1) {
		log_error("x509_from_asn: BIO_write failed\n");
		goto end;
	}
	scert = d2i_X509_bio(certh, NULL);
	if (!scert) {
		log_print("x509_from_asn: d2i_X509_bio failed\n");
		goto end;
	}
end:
	BIO_free(certh);
	return scert;
}

/*
 * Obtain a certificate from an acceptable CA.
 * XXX We don't check if the certificate we find is from an accepted CA.
 */
int
x509_cert_obtain(u_int8_t *id, size_t id_len, void *data, u_int8_t **cert,
    u_int32_t *certlen)
{
	struct x509_aca *aca = data;
	X509		*scert;

	if (aca)
		LOG_DBG((LOG_CRYPTO, 60, "x509_cert_obtain: "
		    "acceptable certificate authorities here"));

	/* We need our ID to find a certificate.  */
	if (!id) {
		log_print("x509_cert_obtain: ID is missing");
		return 0;
	}
	scert = x509_hash_find(id, id_len);
	if (!scert)
		return 0;

	x509_serialize(scert, cert, certlen);
	if (!*cert)
		return 0;
	return 1;
}

/* Returns a pointer to the subjectAltName information of X509 certificate.  */
int
x509_cert_subjectaltname(X509 *scert, u_int8_t **altname, u_int32_t *len)
{
	X509_EXTENSION		*subjectaltname;
	ASN1_OCTET_STRING	*sanasn1data;
	u_int8_t		*sandata;
	int			 extpos, santype, sanlen;

	extpos = X509_get_ext_by_NID(scert, NID_subject_alt_name, -1);
	if (extpos == -1) {
		log_print("x509_cert_subjectaltname: "
		    "certificate does not contain subjectAltName");
		return 0;
	}
	subjectaltname = X509_get_ext(scert, extpos);
	sanasn1data = X509_EXTENSION_get_data(subjectaltname);

	if (!subjectaltname || !sanasn1data || !sanasn1data->data ||
	    sanasn1data->length < 4) {
		log_print("x509_cert_subjectaltname: invalid "
		    "subjectaltname extension");
		return 0;
	}
	/* SSL does not handle unknown ASN stuff well, do it by hand.  */
	sandata = sanasn1data->data;
	santype = sandata[2] & 0x3f;
	sanlen = sandata[3];
	sandata += 4;

	/*
	 * The test here used to be !=, but some certificates can include
	 * extra stuff in subjectAltName, so we will just take the first
	 * salen bytes, and not worry about what follows.
	 */
	if (sanlen + 4 > sanasn1data->length) {
		log_print("x509_cert_subjectaltname: subjectaltname invalid "
		    "length");
		return 0;
	}
	*len = sanlen;
	*altname = sandata;
	return santype;
}

int
x509_cert_get_subjects(void *scert, int *cnt, u_int8_t ***id,
    u_int32_t **id_len)
{
	X509		*cert = scert;
	X509_NAME	*subject;
	int		type;
	u_int8_t	*altname;
	u_int32_t	altlen;
	u_int8_t	*buf = 0;
	unsigned char	*ubuf;
	int		i;

	*id = 0;
	*id_len = 0;

	/*
	 * XXX There can be a collection of subjectAltNames, but for now I
	 * only return the subjectName and a single subjectAltName, if
	 * present.
	 */
	type = x509_cert_subjectaltname(cert, &altname, &altlen);
	if (!type) {
		*cnt = 1;
		altlen = 0;
	} else
		*cnt = 2;

	*id = calloc(*cnt, sizeof **id);
	if (!*id) {
		log_print("x509_cert_get_subject: malloc (%lu) failed",
		    *cnt * (unsigned long)sizeof **id);
		*cnt = 0;
		goto fail;
	}
	*id_len = calloc(*cnt, sizeof **id_len);
	if (!*id_len) {
		log_print("x509_cert_get_subject: malloc (%lu) failed",
		    *cnt * (unsigned long)sizeof **id_len);
		goto fail;
	}
	/* Stash the subjectName into the first slot.  */
	subject = X509_get_subject_name(cert);
	if (!subject)
		goto fail;

	(*id_len)[0] =
		ISAKMP_ID_DATA_OFF + i2d_X509_NAME(subject, NULL) -
		    ISAKMP_GEN_SZ;
	(*id)[0] = malloc((*id_len)[0]);
	if (!(*id)[0]) {
		log_print("x509_cert_get_subject: malloc (%d) failed",
		    (*id_len)[0]);
		goto fail;
	}
	SET_ISAKMP_ID_TYPE((*id)[0] - ISAKMP_GEN_SZ, IPSEC_ID_DER_ASN1_DN);
	ubuf = (*id)[0] + ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ;
	i2d_X509_NAME(subject, &ubuf);

	if (altlen) {
		/* Stash the subjectAltName into the second slot.  */
		buf = malloc(altlen + ISAKMP_ID_DATA_OFF);
		if (!buf) {
			log_print("x509_cert_get_subject: malloc (%d) failed",
			    altlen + ISAKMP_ID_DATA_OFF);
			goto fail;
		}
		switch (type) {
		case X509v3_DNS_NAME:
			SET_ISAKMP_ID_TYPE(buf, IPSEC_ID_FQDN);
			break;

		case X509v3_RFC_NAME:
			SET_ISAKMP_ID_TYPE(buf, IPSEC_ID_USER_FQDN);
			break;

		case X509v3_IP_ADDR:
			/*
			 * XXX I dislike the numeric constants, but I don't
			 * know what we should use otherwise.
			 */
			switch (altlen) {
			case 4:
				SET_ISAKMP_ID_TYPE(buf, IPSEC_ID_IPV4_ADDR);
				break;

			case 16:
				SET_ISAKMP_ID_TYPE(buf, IPSEC_ID_IPV6_ADDR);
				break;

			default:
				log_print("x509_cert_get_subject: invalid "
				    "subjectAltName IPaddress length %d ",
				    altlen);
				goto fail;
			}
			break;
		}

		SET_IPSEC_ID_PROTO(buf + ISAKMP_ID_DOI_DATA_OFF, 0);
		SET_IPSEC_ID_PORT(buf + ISAKMP_ID_DOI_DATA_OFF, 0);
		memcpy(buf + ISAKMP_ID_DATA_OFF, altname, altlen);

		(*id_len)[1] = ISAKMP_ID_DATA_OFF + altlen - ISAKMP_GEN_SZ;
		(*id)[1] = malloc((*id_len)[1]);
		if (!(*id)[1]) {
			log_print("x509_cert_get_subject: malloc (%d) failed",
			    (*id_len)[1]);
			goto fail;
		}
		memcpy((*id)[1], buf + ISAKMP_GEN_SZ, (*id_len)[1]);

		free(buf);
		buf = 0;
	}
	return 1;

fail:
	for (i = 0; i < *cnt; i++)
		free((*id)[i]);
	free(*id);
	free(*id_len);
	free(buf);
	return 0;
}

int
x509_cert_get_key(void *scert, void *keyp)
{
	X509		*cert = scert;
	EVP_PKEY	*key;

	key = X509_get_pubkey(cert);

	/* Check if we got the right key type.  */
	if (EVP_PKEY_id(key) != EVP_PKEY_RSA) {
		log_print("x509_cert_get_key: public key is not a RSA key");
		X509_free(cert);
		return 0;
	}
	*(RSA **)keyp = RSAPublicKey_dup(EVP_PKEY_get0_RSA(key));

	return *(RSA **)keyp == NULL ? 0 : 1;
}

void *
x509_cert_dup(void *scert)
{
	return X509_dup(scert);
}

void
x509_serialize(void *scert, u_int8_t **data, u_int32_t *datalen)
{
	u_int8_t	*p;

	*datalen = i2d_X509((X509 *)scert, NULL);
	*data = p = malloc(*datalen);
	if (!p) {
		log_error("x509_serialize: malloc (%d) failed", *datalen);
		return;
	}
	*datalen = i2d_X509((X509 *)scert, &p);
}

/* From cert to printable */
char *
x509_printable(void *cert)
{
	char		*s;
	u_int8_t	*data;
	u_int32_t	datalen;

	x509_serialize(cert, &data, &datalen);
	if (!data)
		return 0;

	s = raw2hex(data, datalen);
	free(data);
	return s;
}

/* From printable to cert */
void *
x509_from_printable(char *cert)
{
	u_int8_t	*buf;
	int		plen, ret;
	void		*foo;

	plen = (strlen(cert) + 1) / 2;
	buf = malloc(plen);
	if (!buf) {
		log_error("x509_from_printable: malloc (%d) failed", plen);
		return 0;
	}
	ret = hex2raw(cert, buf, plen);
	if (ret == -1) {
		free(buf);
		log_print("x509_from_printable: badly formatted cert");
		return 0;
	}
	foo = x509_cert_get(buf, plen);
	free(buf);
	if (!foo)
		log_print("x509_from_printable: "
		    "could not retrieve certificate");
	return foo;
}

char *
x509_DN_string(u_int8_t *asn1, size_t sz)
{
	X509_NAME	*name;
	const u_int8_t	*p = asn1;
	char		buf[256];	/* XXX Just a guess at a maximum length.  */
	long len = sz;

	name = d2i_X509_NAME(NULL, &p, len);
	if (!name) {
		log_print("x509_DN_string: d2i_X509_NAME failed");
		return 0;
	}
	if (!X509_NAME_oneline(name, buf, sizeof buf - 1)) {
		log_print("x509_DN_string: X509_NAME_oneline failed");
		X509_NAME_free(name);
		return 0;
	}
	X509_NAME_free(name);
	buf[sizeof buf - 1] = '\0';
	return strdup(buf);
}

/* Number of CAs we trust (to decide whether we can send CERT_REQ) */
int
x509_ca_count(void)
{
	return n_x509_cas;
}
