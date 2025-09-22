/* $OpenBSD: ocspcheck.c,v 1.34 2024/12/04 07:58:51 tb Exp $ */

/*
 * Copyright (c) 2017,2020 Bob Beck <beck@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ocsp.h>
#include <openssl/posix_time.h>
#include <openssl/ssl.h>

#include "http.h"

#define MAXAGE_SEC (14*24*60*60)
#define JITTER_SEC (60)
#define OCSP_MAX_RESPONSE_SIZE (20480)

typedef struct ocsp_request {
	STACK_OF(X509) *fullchain;
	OCSP_REQUEST *req;
	char *url;
	unsigned char *data;
	size_t size;
	int nonce;
} ocsp_request;

int verbose;
#define vspew(fmt, ...) \
	do { if (verbose >= 1) fprintf(stderr, fmt, __VA_ARGS__); } while (0)
#define dspew(fmt, ...) \
	do { if (verbose >= 2) fprintf(stderr, fmt, __VA_ARGS__); } while (0)

#define MAX_SERVERS_DNS 8

struct addr {
	int	 family; /* 4 for PF_INET, 6 for PF_INET6 */
	char	 ip[INET6_ADDRSTRLEN];
};

static ssize_t
host_dns(const char *s, struct addr vec[MAX_SERVERS_DNS])
{
	struct addrinfo		 hints, *res0, *res;
	int			 error;
	ssize_t			 vecsz;
	struct sockaddr		*sa;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM; /* DUMMY */

	error = getaddrinfo(s, NULL, &hints, &res0);

	if (error == EAI_AGAIN ||
#ifdef EAI_NODATA
	    error == EAI_NODATA ||
#endif
	    error == EAI_NONAME)
		return 0;

	if (error) {
		warnx("%s: parse error: %s", s, gai_strerror(error));
		return -1;
	}

	for (vecsz = 0, res = res0;
	    res != NULL && vecsz < MAX_SERVERS_DNS;
	    res = res->ai_next) {
		if (res->ai_family != AF_INET &&
		    res->ai_family != AF_INET6)
			continue;

		sa = res->ai_addr;

		if (res->ai_family == AF_INET) {
			vec[vecsz].family = 4;
			inet_ntop(AF_INET,
			    &(((struct sockaddr_in *)sa)->sin_addr),
				vec[vecsz].ip, INET6_ADDRSTRLEN);
		} else {
			vec[vecsz].family = 6;
			inet_ntop(AF_INET6,
			    &(((struct sockaddr_in6 *)sa)->sin6_addr),
			    vec[vecsz].ip, INET6_ADDRSTRLEN);
		}

		dspew("DNS returns %s for %s\n", vec[vecsz].ip, s);
		vecsz++;
	}

	freeaddrinfo(res0);
	return vecsz;
}

/*
 * Extract the domain and port from a URL.
 * The url must be formatted as schema://address[/stuff].
 * This returns NULL on failure.
 */
static char *
url2host(const char *host, short *port, char **path)
{
	char	*url, *ep;

	/* We only understand HTTP and HTTPS. */

	if (strncmp(host, "https://", 8) == 0) {
		*port = 443;
		if ((url = strdup(host + 8)) == NULL) {
			warn("strdup");
			return (NULL);
		}
	} else if (strncmp(host, "http://", 7) == 0) {
		*port = 80;
		if ((url = strdup(host + 7)) == NULL) {
			warn("strdup");
			return (NULL);
		}
	} else {
		warnx("%s: unknown schema", host);
		return (NULL);
	}

	/* Terminate path part. */

	if ((ep = strchr(url, '/')) != NULL) {
		*path = strdup(ep);
		*ep = '\0';
	} else
		*path = strdup("/");

	if (*path == NULL) {
		warn("strdup");
		free(url);
		return (NULL);
	}

	/* Check to see if there is a port in the url */
	if ((ep = strchr(url, ':')) != NULL) {
		const char *errstr;
		short pp;
		pp = strtonum(ep + 1, 1, SHRT_MAX, &errstr);
		if (errstr != NULL) {
			warnx("error parsing port from '%s': %s", url, errstr);
			free(url);
			free(*path);
			return NULL;
		}
		*port = pp;
		*ep = '\0';
	}

	return (url);
}

static time_t
parse_ocsp_time(ASN1_GENERALIZEDTIME *gt)
{
	struct tm tm;
	time_t rv = -1;

	if (gt == NULL)
		return -1;
	/* RFC 6960 specifies that all times in OCSP must be GENERALIZEDTIME */
	if (!ASN1_GENERALIZEDTIME_check(gt))
		return -1;
	if (!ASN1_TIME_to_tm(gt, &tm))
		return -1;
	if (!OPENSSL_timegm(&tm, &rv))
		return -1;
	return rv;
}

static X509_STORE *
read_cacerts(const char *file, const char *dir)
{
	X509_STORE *store = NULL;
	X509_LOOKUP *lookup;

	if (file == NULL && dir == NULL) {
		warnx("No CA certs to load");
		goto end;
	}
	if ((store = X509_STORE_new()) == NULL) {
		warnx("Malloc failed");
		goto end;
	}
	if (file != NULL) {
		if ((lookup = X509_STORE_add_lookup(store,
		    X509_LOOKUP_file())) == NULL) {
			warnx("Unable to load CA cert file");
			goto end;
		}
		if (!X509_LOOKUP_load_file(lookup, file, X509_FILETYPE_PEM)) {
			warnx("Unable to load CA certs from file %s", file);
			goto end;
		}
	}
	if (dir != NULL) {
		if ((lookup = X509_STORE_add_lookup(store,
		    X509_LOOKUP_hash_dir())) == NULL) {
			warnx("Unable to load CA cert directory");
			goto end;
		}
		if (!X509_LOOKUP_add_dir(lookup, dir, X509_FILETYPE_PEM)) {
			warnx("Unable to load CA certs from directory %s", dir);
			goto end;
		}
	}
	return store;

 end:
	X509_STORE_free(store);
	return NULL;
}

static STACK_OF(X509) *
read_fullchain(const char *file, int *count)
{
	int i;
	BIO *bio;
	STACK_OF(X509_INFO) *xis = NULL;
	X509_INFO *xi;
	STACK_OF(X509) *rv = NULL;

	*count = 0;

	if ((bio = BIO_new_file(file, "r")) == NULL) {
		warn("Unable to read a certificate from %s", file);
		goto end;
	}
	if ((xis = PEM_X509_INFO_read_bio(bio, NULL, NULL, NULL)) == NULL) {
		warnx("Unable to read PEM format from %s", file);
		goto end;
	}
	if (sk_X509_INFO_num(xis) <= 0) {
		warnx("No certificates in file %s", file);
		goto end;
	}
	if ((rv = sk_X509_new_null()) == NULL) {
		warnx("malloc failed");
		goto end;
	}

	for (i = 0; i < sk_X509_INFO_num(xis); i++) {
		xi = sk_X509_INFO_value(xis, i);
		if (xi->x509 == NULL)
			continue;
		if (!sk_X509_push(rv, xi->x509)) {
			warnx("unable to build x509 chain");
			sk_X509_pop_free(rv, X509_free);
			rv = NULL;
			goto end;
		}
		xi->x509 = NULL;
		(*count)++;
	}
 end:
	BIO_free(bio);
	sk_X509_INFO_pop_free(xis, X509_INFO_free);
	return rv;
}

static inline X509 *
cert_from_chain(STACK_OF(X509) *fullchain)
{
	return sk_X509_value(fullchain, 0);
}

static const X509 *
issuer_from_chain(STACK_OF(X509) *fullchain)
{
	const X509 *cert;
	X509_NAME *issuer_name;

	cert = cert_from_chain(fullchain);
	if ((issuer_name = X509_get_issuer_name(cert)) == NULL)
		return NULL;

	return X509_find_by_subject(fullchain, issuer_name);
}

static ocsp_request *
ocsp_request_new_from_cert(const char *cadir, char *file, int nonce)
{
	X509 *cert;
	int count = 0;
	OCSP_CERTID *id = NULL;
	ocsp_request *request = NULL;
	const EVP_MD *cert_id_md = NULL;
	const X509 *issuer;
	STACK_OF(OPENSSL_STRING) *urls = NULL;

	if ((request = calloc(1, sizeof(ocsp_request))) == NULL) {
		warn("malloc");
		goto err;
	}

	if ((request->req = OCSP_REQUEST_new()) == NULL)
		goto err;

	request->fullchain = read_fullchain(file, &count);
	if (cadir == NULL) {
		/* Drop rpath from pledge, we don't need to read anymore */
		if (pledge("stdio inet dns", NULL) == -1)
			err(1, "pledge");
	}
	if (request->fullchain == NULL) {
		warnx("Unable to read cert chain from file %s", file);
		goto err;
	}
	if (count <= 1) {
		warnx("File %s does not contain a cert chain", file);
		goto err;
	}
	if ((cert = cert_from_chain(request->fullchain)) == NULL) {
		warnx("No certificate found in %s", file);
		goto err;
	}
	if ((issuer = issuer_from_chain(request->fullchain)) == NULL) {
		warnx("Unable to find issuer for cert in %s", file);
		goto err;
	}

	urls = X509_get1_ocsp(cert);
	if (urls == NULL || sk_OPENSSL_STRING_num(urls) <= 0) {
		warnx("Certificate in %s contains no OCSP url", file);
		goto err;
	}
	if ((request->url = strdup(sk_OPENSSL_STRING_value(urls, 0))) == NULL)
		goto err;
	X509_email_free(urls);
	urls = NULL;

	cert_id_md = EVP_sha1(); /* XXX. This sucks but OCSP is poopy */
	if ((id = OCSP_cert_to_id(cert_id_md, cert, issuer)) == NULL) {
		warnx("Unable to get certificate id from cert in %s", file);
		goto err;
	}
	if (OCSP_request_add0_id(request->req, id) == NULL) {
		warnx("Unable to add certificate id to request");
		goto err;
	}
	id = NULL;

	request->nonce = nonce;
	if (request->nonce)
		OCSP_request_add1_nonce(request->req, NULL, -1);

	if ((request->size = i2d_OCSP_REQUEST(request->req,
	    &request->data)) <= 0) {
		warnx("Unable to encode ocsp request");
		goto err;
	}
	if (request->data == NULL) {
		warnx("Unable to allocate memory");
		goto err;
	}
	return request;

 err:
	if (request != NULL) {
		sk_X509_pop_free(request->fullchain, X509_free);
		free(request->url);
		OCSP_REQUEST_free(request->req);
		free(request->data);
	}
	X509_email_free(urls);
	OCSP_CERTID_free(id);
	free(request);
	return NULL;
}


int
validate_response(char *buf, size_t size, ocsp_request *request,
    X509_STORE *store, char *host, char *file)
{
	ASN1_GENERALIZEDTIME *revtime = NULL, *thisupd = NULL, *nextupd = NULL;
	const unsigned char **p = (const unsigned char **)&buf;
	int status, cert_status = 0, crl_reason = 0;
	time_t now, rev_t = -1, this_t, next_t;
	OCSP_RESPONSE *resp = NULL;
	OCSP_BASICRESP *bresp = NULL;
	OCSP_CERTID *cid = NULL;
	const X509 *cert, *issuer;
	int ret = 0;

	if ((cert = cert_from_chain(request->fullchain)) == NULL) {
		warnx("No certificate found in %s", file);
		goto err;
	}
	if ((issuer = issuer_from_chain(request->fullchain)) == NULL) {
		warnx("Unable to find certificate issuer for cert in %s", file);
		goto err;
	}
	if ((cid = OCSP_cert_to_id(NULL, cert, issuer)) == NULL) {
		warnx("Unable to get issuer cert/CID in %s", file);
		goto err;
	}

	if ((resp = d2i_OCSP_RESPONSE(NULL, p, size)) == NULL) {
		warnx("OCSP response unserializable from host %s", host);
		goto err;
	}

	if ((bresp = OCSP_response_get1_basic(resp)) == NULL) {
		warnx("Failed to load OCSP response from %s", host);
		goto err;
	}

	if (OCSP_basic_verify(bresp, request->fullchain, store,
		OCSP_TRUSTOTHER) != 1) {
		warnx("OCSP verify failed from %s", host);
		goto err;
	}
	dspew("OCSP response signature validated from %s\n", host);

	status = OCSP_response_status(resp);
	if (status != OCSP_RESPONSE_STATUS_SUCCESSFUL) {
		warnx("OCSP Failure: code %d (%s) from host %s",
		    status, OCSP_response_status_str(status), host);
		goto err;
	}
	dspew("OCSP response status %d from host %s\n", status, host);

	/* Check the nonce if we sent one */

	if (request->nonce) {
		if (OCSP_check_nonce(request->req, bresp) <= 0) {
			warnx("No OCSP nonce, or mismatch, from host %s", host);
			goto err;
		}
	}

	if (OCSP_resp_find_status(bresp, cid, &cert_status, &crl_reason,
	    &revtime, &thisupd, &nextupd) != 1) {
		warnx("OCSP verify failed: no result for cert");
		goto err;
	}

	if (revtime && (rev_t = parse_ocsp_time(revtime)) == -1) {
		warnx("Unable to parse revocation time in OCSP reply");
		goto err;
	}
	/*
	 * Belt and suspenders, Treat it as revoked if there is either
	 * a revocation time, or status revoked.
	 */
	if (rev_t != -1 || cert_status == V_OCSP_CERTSTATUS_REVOKED) {
		warnx("Invalid OCSP reply: certificate is revoked");
		if (rev_t != -1)
			warnx("Certificate revoked at: %s", ctime(&rev_t));
		goto err;
	}
	if ((this_t = parse_ocsp_time(thisupd)) == -1) {
		warnx("unable to parse this update time in OCSP reply");
		goto err;
	}
	if ((next_t = parse_ocsp_time(nextupd)) == -1) {
		warnx("unable to parse next update time in OCSP reply");
		goto err;
	}

	/* Don't allow this update to precede next update */
	if (this_t >= next_t) {
		warnx("Invalid OCSP reply: this update >= next update");
		goto err;
	}

	now = time(NULL);
	/*
	 * Check that this update is not more than JITTER seconds
	 * in the future.
	 */
	if (this_t > now + JITTER_SEC) {
		warnx("Invalid OCSP reply: this update is in the future at %s",
		    ctime(&this_t));
		goto err;
	}

	/*
	 * Check that this update is not more than MAXSEC
	 * in the past.
	 */
	if (this_t < now - MAXAGE_SEC) {
		warnx("Invalid OCSP reply: this update is too old %s",
		    ctime(&this_t));
		goto err;
	}

	/*
	 * Check that next update is still valid
	 */
	if (next_t < now - JITTER_SEC) {
		warnx("Invalid OCSP reply: reply has expired at %s",
		    ctime(&next_t));
		goto err;
	}

	vspew("OCSP response validated from %s\n", host);
	vspew("	   This Update: %s", ctime(&this_t));
	vspew("	   Next Update: %s", ctime(&next_t));
	ret = 1;
 err:
	OCSP_RESPONSE_free(resp);
	OCSP_BASICRESP_free(bresp);
	OCSP_CERTID_free(cid);
	return ret;
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: ocspcheck [-Nv] [-C CAfile] [-i staplefile] "
	    "[-o staplefile] file\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	const char *cafile = NULL, *cadir = NULL;
	char *host = NULL, *path = NULL, *certfile = NULL, *outfile = NULL,
	    *instaple = NULL, *infile = NULL;
	struct addr addrs[MAX_SERVERS_DNS] = {{0}};
	struct source sources[MAX_SERVERS_DNS];
	int i, ch, staplefd = -1, infd = -1, nonce = 1;
	ocsp_request *request = NULL;
	size_t rescount, instaplesz = 0;
	struct httpget *hget;
	X509_STORE *castore;
	ssize_t written, w;
	short port;

	while ((ch = getopt(argc, argv, "C:i:No:v")) != -1) {
		switch (ch) {
		case 'C':
			cafile = optarg;
			break;
		case 'N':
			nonce = 0;
			break;
		case 'o':
			outfile = optarg;
			break;
		case 'i':
			infile = optarg;
			break;
		case 'v':
			verbose++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1 || (certfile = argv[0]) == NULL)
		usage();

	if (outfile != NULL) {
		if (strcmp(outfile, "-") == 0)
			staplefd = STDOUT_FILENO;
		else
			staplefd = open(outfile, O_WRONLY|O_CREAT,
			    S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH);
		if (staplefd < 0)
			err(1, "Unable to open output file %s", outfile);
	}

	if (infile != NULL) {
		if (strcmp(infile, "-") == 0)
			infd = STDIN_FILENO;
		else
			infd = open(infile, O_RDONLY);
		if (infd < 0)
			err(1, "Unable to open input file %s", infile);
		nonce = 0; /* Can't validate a nonce on a saved reply */
	}

	if (cafile == NULL) {
		if (access(X509_get_default_cert_file(), R_OK) == 0)
			cafile = X509_get_default_cert_file();
		if (access(X509_get_default_cert_dir(), F_OK) == 0)
			cadir = X509_get_default_cert_dir();
	}

	if (cafile != NULL) {
		if (unveil(cafile, "r") == -1)
			err(1, "unveil %s", cafile);
	}
	if (cadir != NULL) {
		if (unveil(cadir, "r") == -1)
			err(1, "unveil %s", cadir);
	}
	if (unveil(certfile, "r") == -1)
		err(1, "unveil %s", certfile);

	if (pledge("stdio inet rpath dns", NULL) == -1)
		err(1, "pledge");

	/*
	 * Load our certificate and keystore, and build up an
	 * OCSP request based on the full certificate chain
	 * we have been given to check.
	 */
	if ((castore = read_cacerts(cafile, cadir)) == NULL)
		exit(1);
	if ((request = ocsp_request_new_from_cert(cadir, certfile, nonce))
	    == NULL)
		exit(1);

	dspew("Built an %zu byte ocsp request\n", request->size);

	if ((host = url2host(request->url, &port, &path)) == NULL)
		errx(1, "Invalid OCSP url %s from %s", request->url,
		    certfile);

	if (infd == -1) {
		/* Get a new OCSP response from the indicated server */

		vspew("Using %s to host %s, port %d, path %s\n",
		    port == 443 ? "https" : "http", host, port, path);

		rescount = host_dns(host, addrs);
		for (i = 0; i < rescount; i++) {
			sources[i].ip = addrs[i].ip;
			sources[i].family = addrs[i].family;
		}

		/*
		 * Do an HTTP post to send our request to the OCSP
		 * server, and hopefully get an answer back
		 */
		hget = http_get(sources, rescount, host, port, path,
		    request->data, request->size);
		if (hget == NULL)
			errx(1, "http_get");
		/*
		 * Pledge minimally before fiddling with libcrypto init
		 * routines and parsing untrusted input from someone's OCSP
		 * server.
		 */
		if (cadir == NULL) {
			if (pledge("stdio", NULL) == -1)
				err(1, "pledge");
		} else {
			if (pledge("stdio rpath", NULL) == -1)
				err(1, "pledge");
		}

		dspew("Server at %s returns:\n", host);
		for (i = 0; i < hget->headsz; i++)
			dspew("   [%s]=[%s]\n", hget->head[i].key, hget->head[i].val);
		dspew("	  [Body]=[%zu bytes]\n", hget->bodypartsz);
		if (hget->bodypartsz <= 0)
			errx(1, "No body in reply from %s", host);

		if (hget->code != 200)
			errx(1, "http reply code %d from %s", hget->code, host);

		/*
		 * Validate the OCSP response we got back
		 */
		OPENSSL_add_all_algorithms_noconf();
		if (!validate_response(hget->bodypart, hget->bodypartsz,
			request, castore, host, certfile))
			exit(1);
		instaple = hget->bodypart;
		instaplesz = hget->bodypartsz;
	} else {
		size_t nr = 0;
		instaplesz = 0;

		/*
		 * Pledge minimally before fiddling with libcrypto init
		 */
		if (cadir == NULL) {
			if (pledge("stdio", NULL) == -1)
				err(1, "pledge");
		} else {
			if (pledge("stdio rpath", NULL) == -1)
				err(1, "pledge");
		}

		dspew("Using ocsp response saved in %s:\n", infile);

		/* Use the existing OCSP response saved in infd */
		instaple = calloc(OCSP_MAX_RESPONSE_SIZE, 1);
		if (instaple) {
			while ((nr = read(infd, instaple + instaplesz,
			    OCSP_MAX_RESPONSE_SIZE - instaplesz)) != -1 &&
			    nr != 0)
				instaplesz += nr;
		}
		if (instaplesz == 0)
			exit(1);
		/*
		 * Validate the OCSP staple we read in.
		 */
		OPENSSL_add_all_algorithms_noconf();
		if (!validate_response(instaple, instaplesz,
			request, castore, host, certfile))
			exit(1);
	}

	/*
	 * If we have been given a place to save a staple,
	 * write out the DER format response to the staplefd
	 */
	if (staplefd >= 0) {
		while (ftruncate(staplefd, 0) < 0) {
			if (errno == EINVAL)
				break;
			if (errno != EINTR && errno != EAGAIN)
				err(1, "Write of OCSP response failed");
		}
		written = 0;
		while (written < instaplesz) {
			w = write(staplefd, instaple + written,
			    instaplesz - written);
			if (w == -1) {
				if (errno != EINTR && errno != EAGAIN)
					err(1, "Write of OCSP response failed");
			} else
				written += w;
		}
		close(staplefd);
	}
	exit(0);
}
