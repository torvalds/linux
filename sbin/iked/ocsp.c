/*	$OpenBSD: ocsp.c,v 1.25 2024/01/17 08:25:02 claudio Exp $ */

/*
 * Copyright (c) 2014 Markus Friedl
 * Copyright (c) 2005 Marco Pfatschbacher
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/stat.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>

#include <openssl/pem.h>
#include <openssl/ocsp.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <event.h>

#include "iked.h"

struct iked_ocsp {
	struct iked		*ocsp_env;	/* back pointer to env */
	struct iked_sahdr	 ocsp_sh;	/* ike sa */
	uint8_t			 ocsp_type;	/* auth type */
	struct iked_socket	*ocsp_sock;	/* socket to ocsp responder */
	BIO			*ocsp_cbio;	/* matching OpenSSL obj */
	OCSP_CERTID		*ocsp_id;	/* ocsp-id for cert */
	OCSP_REQUEST		*ocsp_req;	/* ocsp-request */
	OCSP_REQ_CTX		*ocsp_req_ctx;	/* async ocsp-request */
};

struct ocsp_connect {
	struct iked_sahdr	 oc_sh;
	struct iked_socket	 oc_sock;
	char			*oc_path;
	char			*oc_url;
};

#define OCSP_TIMEOUT	30

/* priv */
void		 ocsp_connect_cb(int, short, void *);
int		 ocsp_connect_finish(struct iked *, int, struct ocsp_connect *);

/* unpriv */
void		 ocsp_free(struct iked_ocsp *);
void		 ocsp_callback(int, short, void *);
void		 ocsp_parse_response(struct iked_ocsp *, OCSP_RESPONSE *);
STACK_OF(X509)	*ocsp_load_certs(const char *);
int		 ocsp_validate_finish(struct iked_ocsp *, int);


/* priv */

/* async connect to configure ocsp-responder */
int
ocsp_connect(struct iked *env, struct imsg *imsg)
{
	struct ocsp_connect	*oc = NULL;
	struct iked_sahdr	 sh;
	struct addrinfo		 hints, *res0 = NULL, *res;
	struct timeval		 tv;
	uint8_t			*ptr;
	size_t			 len;
	char			*host = NULL, *port = NULL, *path = NULL;
	char			*url, *freeme = NULL;
	int			use_ssl, fd = -1, ret = -1, error;

	IMSG_SIZE_CHECK(imsg, &sh);

	ptr = (uint8_t *)imsg->data;
	len = IMSG_DATA_SIZE(imsg);

	memcpy(&sh, ptr, sizeof(sh));

	ptr += sizeof(sh);
	len -= sizeof(sh);

	if (len > 0)
		url = freeme = get_string(ptr, len);
	else if (env->sc_ocsp_url)
		url = env->sc_ocsp_url;
	else {
		log_warnx("%s: no ocsp url", SPI_SH(&sh, __func__));
		goto done;
	}
	if (!OCSP_parse_url(url, &host, &port, &path, &use_ssl)) {
		log_warnx("%s: error parsing OCSP-request-URL: %s",
		    SPI_SH(&sh, __func__), url);
		goto done;
	}
	if (use_ssl) {
		log_warnx("%s: OCSP over SSL not supported: %s",
		    SPI_SH(&sh, __func__), url);
		goto done;
	}

	if ((fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) == -1) {
		log_debug("%s: socket failed", SPI_SH(&sh, __func__));
		goto done;
	}
	if ((oc = calloc(1, sizeof(*oc))) == NULL) {
		log_debug("%s: calloc failed", __func__);
		goto done;
	}

	bzero(&hints, sizeof(struct addrinfo));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(host, port, &hints, &res0);
	if (error) {
		log_warn("%s: getaddrinfo(%s, %s) failed",
		    SPI_SH(&sh, __func__), host, port);
		goto done;
	}
	/* XXX just pick the first answer. we could loop instead */
	for (res = res0; res; res = res->ai_next)
		if (res->ai_family == AF_INET)
			break;
	if (res == NULL) {
		log_debug("%s: no addr to connect to for %s:%s",
		    SPI_SH(&sh, __func__), host, port);
		goto done;
	}

	oc->oc_sock.sock_fd = fd;
	oc->oc_sock.sock_env = env;
	oc->oc_sh = sh;
	oc->oc_path = path;
	oc->oc_url = strdup(url);
	if (oc->oc_url == NULL) {
		log_warn("%s: strdup failed", SPI_SH(&sh, __func__));
		goto done;
	}
	path = NULL;

	log_debug("%s: connect(%s, %s)", __func__, host, port);
	if (connect(fd, res->ai_addr, res->ai_addrlen) == -1) {
		/* register callback for ansync connect */
		if (errno == EINPROGRESS) {
			tv.tv_sec = OCSP_TIMEOUT;
			tv.tv_usec = 0;
			event_set(&oc->oc_sock.sock_ev, fd, EV_WRITE,
			    ocsp_connect_cb, oc);
			event_add(&oc->oc_sock.sock_ev, &tv);
			ret = 0;
		} else
			log_warn("%s: connect(%s, %s)",
			    SPI_SH(&oc->oc_sh, __func__), host, port);
	} else {
		ocsp_connect_finish(env, fd, oc);
		ret = 0;
	}
 done:
	if (res0)
		freeaddrinfo(res0);
	free(freeme);
	free(host);
	free(port);
	free(path);
	if (ret == -1) {
		ocsp_connect_finish(env, -1, oc);
		if (fd >= 0)
			close(fd);
	}
	return (ret);
}

/* callback triggered if connection to ocsp-responder completes/fails */
void
ocsp_connect_cb(int fd, short event, void *arg)
{
	struct ocsp_connect	*oc = arg;
	int			 error, send_fd = -1;
	socklen_t		 len;

	if (event == EV_TIMEOUT) {
		log_info("%s: timeout, giving up",
		    SPI_SH(&oc->oc_sh, __func__));
		goto done;
	}

	len = sizeof(error);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) == -1) {
		log_warn("%s: getsockopt SOL_SOCKET SO_ERROR",
		    SPI_SH(&oc->oc_sh, __func__));
	} else if (error) {
		log_warnx("%s: error while connecting: %s",
		    SPI_SH(&oc->oc_sh, __func__), strerror(error));
	} else {
		send_fd = fd;
	}
 done:
	ocsp_connect_finish(oc->oc_sock.sock_env, send_fd, oc);

	/* if we did not send the fd, we need to close it ourself */
	if (send_fd == -1)
		close(fd);
}

/* send FD+path or error back to CA process */
int
ocsp_connect_finish(struct iked *env, int fd, struct ocsp_connect *oc)
{
	struct iovec		 iov[2];
	int			 iovcnt = 0, ret;

	iov[iovcnt].iov_base = &oc->oc_sh;
	iov[iovcnt].iov_len = sizeof(oc->oc_sh);
	iovcnt++;

	if (oc && fd >= 0) {
		/* the imsg framework will close the FD after send */
		iov[iovcnt].iov_base = oc->oc_path;
		iov[iovcnt].iov_len = strlen(oc->oc_path);
		iovcnt++;
		ret = proc_composev_imsg(&env->sc_ps, PROC_CERT, -1,
		    IMSG_OCSP_FD, -1, fd, iov, iovcnt);
	} else {
		if (oc)
			log_info("%s: connect failed for %s",
			    SPI_SH(&oc->oc_sh, __func__),
			    oc->oc_url ? oc->oc_url : "unknown");
		else
			log_info("%s: connect failed", __func__);
		ret = proc_composev_imsg(&env->sc_ps, PROC_CERT, -1,
		    IMSG_OCSP_FD, -1, -1, iov, iovcnt);
		if (fd >= 0)
			close(fd);
	}
	if (oc) {
		free(oc->oc_url);
		free(oc->oc_path);
		free(oc);
	}
	return (ret);
}


/* unpriv */

/* validate the certifcate stored in 'data' by querying the ocsp-responder */
int
ocsp_validate_cert(struct iked *env, void *data, size_t len,
    struct iked_sahdr sh, uint8_t type, X509 *issuer)
{
	struct iovec		 iov[2];
	STACK_OF(OPENSSL_STRING) *aia; /* Authority Information Access */
	struct iked_ocsp_entry	*ioe;
	struct iked_ocsp	*ocsp;
	OCSP_CERTID		*id = NULL;
	char			*url;
	BIO			*rawcert = NULL;
	X509			*cert = NULL;
	int			 ret, iovcnt = 0;

	if (issuer == NULL)
		return (-1);
	if ((ioe = calloc(1, sizeof(*ioe))) == NULL)
		return (-1);
	if ((ocsp = calloc(1, sizeof(*ocsp))) == NULL) {
		free(ioe);
		return (-1);
	}

	ocsp->ocsp_env = env;
	ocsp->ocsp_sh = sh;
	ocsp->ocsp_type = type;

	if ((rawcert = BIO_new_mem_buf(data, len)) == NULL ||
	    (cert = d2i_X509_bio(rawcert, NULL)) == NULL ||
	    (ocsp->ocsp_cbio = BIO_new(BIO_s_socket())) == NULL ||
	    (ocsp->ocsp_req = OCSP_REQUEST_new()) == NULL ||
	    (id = OCSP_cert_to_id(NULL, cert, issuer)) == NULL ||
	    !OCSP_request_add0_id(ocsp->ocsp_req, id))
		goto err;

	/* id is owned by and freed together with ocsp_req */
	ocsp->ocsp_id = id;

	BIO_free(rawcert);
	X509_free(cert);

	ioe->ioe_ocsp = ocsp;
	TAILQ_INSERT_TAIL(&env->sc_ocsp, ioe, ioe_entry);

	/* pass SA header */
	iov[iovcnt].iov_base = &ocsp->ocsp_sh;
	iov[iovcnt].iov_len = sizeof(ocsp->ocsp_sh);
	iovcnt++;

	/* pass optional ocsp-url from issuer */
	if ((aia = X509_get1_ocsp(issuer)) != NULL) {
		url = sk_OPENSSL_STRING_value(aia, 0);
		log_debug("%s: aia %s", __func__, url);
		iov[iovcnt].iov_base = url;
		iov[iovcnt].iov_len = strlen(url);
		iovcnt++;
	}
	/* request connection to ocsp-responder */
	ret = proc_composev(&env->sc_ps, PROC_PARENT, IMSG_OCSP_FD,
	    iov, iovcnt);

	X509_email_free(aia);	/* free stack of openssl strings */

	return (ret);

 err:
	ca_sslerror(__func__);
	free(ioe);
	BIO_free(rawcert);
	X509_free(cert);
	OCSP_CERTID_free(id);
	ocsp_validate_finish(ocsp, 0);	/* failed */
	return (-1);
}

/* free ocsp query context */
void
ocsp_free(struct iked_ocsp *ocsp)
{
	if (ocsp != NULL) {
		if (ocsp->ocsp_sock != NULL) {
			close(ocsp->ocsp_sock->sock_fd);
			free(ocsp->ocsp_sock);
		}
		BIO_free_all(ocsp->ocsp_cbio);
		OCSP_REQ_CTX_free(ocsp->ocsp_req_ctx);
		OCSP_REQUEST_free(ocsp->ocsp_req);
		free(ocsp);
	}
}

/* we got a connection to the ocsp responder */
int
ocsp_receive_fd(struct iked *env, struct imsg *imsg)
{
	struct iked_ocsp_entry	*ioe = NULL;
	struct iked_ocsp	*ocsp = NULL, *ocsp_tmp;
	struct iked_socket	*sock;
	struct iked_sahdr	 sh;
	struct timeval		 tv;
	uint8_t			*ptr;
	char			*path = NULL;
	size_t			 len;
	int			 fd, ret = -1;

	IMSG_SIZE_CHECK(imsg, &sh);

	ptr = (uint8_t *)imsg->data;
	len = IMSG_DATA_SIZE(imsg);

	memcpy(&sh, ptr, sizeof(sh));

	ptr += sizeof(sh);
	len -= sizeof(sh);

	TAILQ_FOREACH(ioe, &env->sc_ocsp, ioe_entry) {
		ocsp_tmp = ioe->ioe_ocsp;
		if (memcmp(&ocsp_tmp->ocsp_sh, &sh, sizeof(sh)) == 0)
			break;
	}
	if (ioe == NULL) {
		log_debug("%s: no pending request found", __func__);
		if ((fd = imsg_get_fd(imsg)) != -1)	/* XXX */
			close(fd);
		return (-1);
	}
	TAILQ_REMOVE(&env->sc_ocsp, ioe, ioe_entry);
	ocsp = ioe->ioe_ocsp;
	free(ioe);

	if ((fd = imsg_get_fd(imsg)) == -1)
		goto done;

	if ((sock = calloc(1, sizeof(*sock))) == NULL)
		fatal("ocsp_receive_fd: calloc sock");

	/* note that sock_addr is not set */
	sock->sock_fd = fd;
	sock->sock_env = env;
	ocsp->ocsp_sock = sock;

	log_debug("%s: received socket fd %d", __func__, sock->sock_fd);

	/* fetch 'path' and 'fd' from imsg */
	if ((path = get_string(ptr, len)) == NULL)
		goto done;

	BIO_set_fd(ocsp->ocsp_cbio, sock->sock_fd, BIO_NOCLOSE);

	if ((ocsp->ocsp_req_ctx = OCSP_sendreq_new(ocsp->ocsp_cbio,
	    path, NULL, -1)) == NULL)
		goto done;
	if (!OCSP_REQ_CTX_set1_req(ocsp->ocsp_req_ctx, ocsp->ocsp_req))
		goto done;

	tv.tv_sec = OCSP_TIMEOUT;
	tv.tv_usec = 0;
	event_set(&sock->sock_ev, sock->sock_fd, EV_WRITE, ocsp_callback, ocsp);
	event_add(&sock->sock_ev, &tv);
	ret = 0;
 done:
	if (ret == -1)
		ocsp_validate_finish(ocsp, 0);	/* failed */
	free(path);
	return (ret);
}

/* load a stack of x509 certificates */
STACK_OF(X509)*
ocsp_load_certs(const char *file)
{
	BIO			*bio = NULL;
	STACK_OF(X509)		*certs = NULL;
	STACK_OF(X509_INFO)	*xis = NULL;
	X509_INFO		*xi;
	int			 i;

	if ((bio = BIO_new_file(file, "r")) == NULL) {
		log_warn("%s: BIO_new_file failed for %s",
		    __func__, file);
		return (NULL);
	}
	if ((xis = PEM_X509_INFO_read_bio(bio, NULL, NULL, NULL)) == NULL) {
		ca_sslerror(__func__);
		goto done;
	}
	if ((certs = sk_X509_new_null()) == NULL) {
		log_debug("%s: sk_X509_new_null failed for %s", __func__, file);
		goto done;
	}
	for (i = 0; i < sk_X509_INFO_num(xis); i++) {
		xi = sk_X509_INFO_value(xis, i);
		if (xi->x509) {
			if (!sk_X509_push(certs, xi->x509))
				goto done;
			xi->x509 = NULL;
		}
	}

 done:
	BIO_free(bio);
	sk_X509_INFO_pop_free(xis, X509_INFO_free);
	if (sk_X509_num(certs) <= 0) {
		sk_X509_free(certs);
		certs = NULL;
	}
	return (certs);
}

/* read/write callback that sends the requests and reads the ocsp response */
void
ocsp_callback(int fd, short event, void *arg)
{
	struct iked_ocsp	*ocsp = arg;
	struct iked_socket	*sock = ocsp->ocsp_sock;
	struct timeval		 tv;
	OCSP_RESPONSE		*resp = NULL;

	if (event == EV_TIMEOUT) {
		log_info("%s: timeout, giving up",
		    SPI_SH(&ocsp->ocsp_sh, __func__));
		ocsp_validate_finish(ocsp, 0);
		return;
	}
	/*
	 * Only call OCSP_sendreq_nbio() if should_read/write is
	 * either not requested or read/write can be called.
	 */
	if ((!BIO_should_read(ocsp->ocsp_cbio) || (event & EV_READ)) &&
	    (!BIO_should_write(ocsp->ocsp_cbio) || (event & EV_WRITE)) &&
	    OCSP_sendreq_nbio(&resp, ocsp->ocsp_req_ctx) != -1 ) {
		ocsp_parse_response(ocsp, resp);
		return;
	}
	if (BIO_should_read(ocsp->ocsp_cbio))
		event_set(&sock->sock_ev, sock->sock_fd, EV_READ,
		    ocsp_callback, ocsp);
	else if (BIO_should_write(ocsp->ocsp_cbio))
		event_set(&sock->sock_ev, sock->sock_fd, EV_WRITE,
		    ocsp_callback, ocsp);
	tv.tv_sec = OCSP_TIMEOUT;
	tv.tv_usec = 0;
	event_add(&sock->sock_ev, &tv);
}

/* parse the actual OCSP response */
void
ocsp_parse_response(struct iked_ocsp *ocsp, OCSP_RESPONSE *resp)
{
	struct iked		*env = ocsp->ocsp_env;
	X509_STORE		*store = NULL;
	STACK_OF(X509)		*verify_other = NULL;
	OCSP_BASICRESP		*bs = NULL;
	ASN1_GENERALIZEDTIME	*rev, *thisupd, *nextupd;
	const char		*errstr;
	int			 reason = 0, valid = 0, verify_flags = 0;
	int			 status;

	if (!resp) {
		errstr = "error querying OCSP responder";
		goto done;
	}

	status = OCSP_response_status(resp);
	if (status != OCSP_RESPONSE_STATUS_SUCCESSFUL) {
		errstr = OCSP_response_status_str(status);
		goto done;
	}

	verify_other = ocsp_load_certs(IKED_OCSP_RESPCERT);
	verify_flags |= OCSP_TRUSTOTHER;
	if (!verify_other) {
		errstr = "no verify_other";
		goto done;
	}

	bs = OCSP_response_get1_basic(resp);
	if (!bs) {
		errstr = "error parsing response";
		goto done;
	}

	status = OCSP_check_nonce(ocsp->ocsp_req, bs);
	if (status <= 0) {
		if (status == -1)
			log_warnx("%s: no nonce in response",
			    SPI_SH(&ocsp->ocsp_sh, __func__));
		else {
			errstr = "nonce verify error";
			goto done;
		}
	}

	store = X509_STORE_new();
	status = OCSP_basic_verify(bs, verify_other, store, verify_flags);
	if (status < 0)
		status = OCSP_basic_verify(bs, NULL, store, 0);
	if (status <= 0) {
		ca_sslerror(__func__);
		errstr = "response verify failure";
		goto done;
	}
	log_debug("%s: response verify ok", SPI_SH(&ocsp->ocsp_sh, __func__));

	if (!OCSP_resp_find_status(bs, ocsp->ocsp_id, &status, &reason,
	    &rev, &thisupd, &nextupd)) {
		errstr = "no status found";
		goto done;
	}
	if (env->sc_ocsp_tolerate &&
	    !OCSP_check_validity(thisupd, nextupd, env->sc_ocsp_tolerate,
	    env->sc_ocsp_maxage)) {
		ca_sslerror(SPI_SH(&ocsp->ocsp_sh, __func__));
		errstr = "status times invalid";
		goto done;
	}
	errstr = OCSP_cert_status_str(status);
	if (status == V_OCSP_CERTSTATUS_GOOD) {
		log_debug("%s: status: %s", SPI_SH(&ocsp->ocsp_sh, __func__),
		    errstr);
		valid = 1;
	}
 done:
	if (!valid) {
		log_debug("%s: status: %s", __func__, errstr);
	}
	X509_STORE_free(store);
	sk_X509_pop_free(verify_other, X509_free);
	OCSP_RESPONSE_free(resp);
	OCSP_BASICRESP_free(bs);

	ocsp_validate_finish(ocsp, valid);
}

/*
 * finish the ocsp_validate_cert() RPC by sending the appropriate
 * message back to the IKEv2 process
 */
int
ocsp_validate_finish(struct iked_ocsp *ocsp, int valid)
{
	struct iked		*env = ocsp->ocsp_env;
	struct iovec		 iov[2];
	int			 iovcnt = 2, ret, cmd;

	iov[0].iov_base = &ocsp->ocsp_sh;
	iov[0].iov_len = sizeof(ocsp->ocsp_sh);
	iov[1].iov_base = &ocsp->ocsp_type;
	iov[1].iov_len = sizeof(ocsp->ocsp_type);

	cmd = valid ? IMSG_CERTVALID : IMSG_CERTINVALID;
	ret = proc_composev(&env->sc_ps, PROC_IKEV2, cmd, iov, iovcnt);

	ocsp_free(ocsp);
	return (ret);
}
