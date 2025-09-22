/* dnstap support for NSD */

/*
 * Copyright (c) 2013-2014, Farsight Security, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "dnstap/dnstap_config.h"

#ifdef USE_DNSTAP

#include "config.h"
#include <string.h>
#include <sys/time.h>
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include <errno.h>
#include <unistd.h>
#include "util.h"
#include "options.h"

#include <fstrm.h>
#include <protobuf-c/protobuf-c.h>

#include "dnstap/dnstap.h"
#include "dnstap/dnstap.pb-c.h"

#ifdef HAVE_SSL
#ifdef HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif
#ifdef HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif
#endif

#define DNSTAP_CONTENT_TYPE		"protobuf:dnstap.Dnstap"
#define DNSTAP_INITIAL_BUF_SIZE		256

struct dt_msg {
	void		*buf;
	size_t		len_buf;
	Dnstap__Dnstap	d;
	Dnstap__Message	m;
};

static int
dt_pack(const Dnstap__Dnstap *d, void **buf, size_t *sz)
{
	ProtobufCBufferSimple sbuf;

	memset(&sbuf, 0, sizeof(sbuf));
	sbuf.base.append = protobuf_c_buffer_simple_append;
	sbuf.len = 0;
	sbuf.alloced = DNSTAP_INITIAL_BUF_SIZE;
	sbuf.data = malloc(sbuf.alloced);
	if (sbuf.data == NULL)
		return 0;
	sbuf.must_free_data = 1;

	*sz = dnstap__dnstap__pack_to_buffer(d, (ProtobufCBuffer *) &sbuf);
	if (sbuf.data == NULL)
		return 0;
	*buf = sbuf.data;

	return 1;
}

static void
dt_send(const struct dt_env *env, void *buf, size_t len_buf)
{
	fstrm_res res;
	if (!buf)
		return;
	res = fstrm_iothr_submit(env->iothr, env->ioq, buf, len_buf,
				 fstrm_free_wrapper, NULL);
	if (res != fstrm_res_success)
		free(buf);
}

static void
dt_msg_init(const struct dt_env *env,
	    struct dt_msg *dm,
	    Dnstap__Message__Type mtype)
{
	memset(dm, 0, sizeof(*dm));
	dm->d.base.descriptor = &dnstap__dnstap__descriptor;
	dm->m.base.descriptor = &dnstap__message__descriptor;
	dm->d.type = DNSTAP__DNSTAP__TYPE__MESSAGE;
	dm->d.message = &dm->m;
	dm->m.type = mtype;
	if (env->identity != NULL) {
		dm->d.identity.data = (uint8_t *) env->identity;
		dm->d.identity.len = (size_t) env->len_identity;
		dm->d.has_identity = 1;
	}
	if (env->version != NULL) {
		dm->d.version.data = (uint8_t *) env->version;
		dm->d.version.len = (size_t) env->len_version;
		dm->d.has_version = 1;
	}
}

#ifdef HAVE_SSL
/** TLS writer object for fstrm. */
struct dt_tls_writer {
	/* ip address */
	char* ip;
	/* if connected already */
	int connected;
	/* file descriptor */
	int fd;
	/* TLS context */
	SSL_CTX* ctx;
	/* SSL transport */
	SSL* ssl;
	/* the server name to authenticate */
	char* tls_server_name;
};

void log_crypto_err(const char* str); /* in server.c */

/* Create TLS writer object for fstrm. */
static struct dt_tls_writer*
tls_writer_init(char* ip, char* tls_server_name, char* tls_cert_bundle,
	char* tls_client_key_file, char* tls_client_cert_file)
{
	struct dt_tls_writer* dtw = (struct dt_tls_writer*)calloc(1,
		sizeof(*dtw));
	if(!dtw) return NULL;
	dtw->fd = -1;
	dtw->ip = strdup(ip);
	if(!dtw->ip) {
		free(dtw);
		return NULL;
	}
	dtw->ctx = SSL_CTX_new(SSLv23_client_method());
	if(!dtw->ctx) {
		log_msg(LOG_ERR, "dnstap: SSL_CTX_new failed");
		free(dtw->ip);
		free(dtw);
		return NULL;
	}
#if SSL_OP_NO_SSLv2 != 0
	if((SSL_CTX_set_options(dtw->ctx, SSL_OP_NO_SSLv2) & SSL_OP_NO_SSLv2)
		!= SSL_OP_NO_SSLv2) {
		log_msg(LOG_ERR, "dnstap: could not set SSL_OP_NO_SSLv2");
		SSL_CTX_free(dtw->ctx);
		free(dtw->ip);
		free(dtw);
		return NULL;
	}
#endif
	if((SSL_CTX_set_options(dtw->ctx, SSL_OP_NO_SSLv3) & SSL_OP_NO_SSLv3)
		!= SSL_OP_NO_SSLv3) {
		log_msg(LOG_ERR, "dnstap: could not set SSL_OP_NO_SSLv3");
		SSL_CTX_free(dtw->ctx);
		free(dtw->ip);
		free(dtw);
		return NULL;
	}
#if defined(SSL_OP_NO_RENEGOTIATION)
	/* disable client renegotiation */
	if((SSL_CTX_set_options(dtw->ctx, SSL_OP_NO_RENEGOTIATION) &
		SSL_OP_NO_RENEGOTIATION) != SSL_OP_NO_RENEGOTIATION) {
		log_msg(LOG_ERR, "dnstap: could not set SSL_OP_NO_RENEGOTIATION");
		SSL_CTX_free(dtw->ctx);
		free(dtw->ip);
		free(dtw);
		return NULL;
	}
#endif
	if(tls_client_key_file && tls_client_key_file[0]) {
		if(!SSL_CTX_use_certificate_chain_file(dtw->ctx,
			tls_client_cert_file)) {
			log_msg(LOG_ERR, "dnstap: SSL_CTX_use_certificate_chain_file failed for %s", tls_client_cert_file);
			SSL_CTX_free(dtw->ctx);
			free(dtw->ip);
			free(dtw);
			return NULL;
		}
		if(!SSL_CTX_use_PrivateKey_file(dtw->ctx, tls_client_key_file,
			SSL_FILETYPE_PEM)) {
			log_msg(LOG_ERR, "dnstap: SSL_CTX_use_PrivateKey_file failed for %s", tls_client_key_file);
			SSL_CTX_free(dtw->ctx);
			free(dtw->ip);
			free(dtw);
			return NULL;
		}
		if(!SSL_CTX_check_private_key(dtw->ctx)) {
			log_msg(LOG_ERR, "dnstap: SSL_CTX_check_private_key failed for %s", tls_client_key_file);
			SSL_CTX_free(dtw->ctx);
			free(dtw->ip);
			free(dtw);
			return NULL;
		}
	}
	if(tls_cert_bundle && tls_cert_bundle[0]) {
		if(!SSL_CTX_load_verify_locations(dtw->ctx, tls_cert_bundle, NULL)) {
			log_msg(LOG_ERR, "dnstap: SSL_CTX_load_verify_locations failed for %s", tls_cert_bundle);
			SSL_CTX_free(dtw->ctx);
			free(dtw->ip);
			free(dtw);
			return NULL;
		}
		if(SSL_CTX_set_default_verify_paths(dtw->ctx) != 1) {
			log_msg(LOG_ERR, "dnstap: SSL_CTX_set_default_verify_paths failed");
			SSL_CTX_free(dtw->ctx);
			free(dtw->ip);
			free(dtw);
			return NULL;
		}
		SSL_CTX_set_verify(dtw->ctx, SSL_VERIFY_PEER, NULL);
	}
	if(tls_server_name) {
		dtw->tls_server_name = strdup(tls_server_name);
		if(!dtw->tls_server_name) {
				log_msg(LOG_ERR, "dnstap: strdup failed");
				SSL_CTX_free(dtw->ctx);
				free(dtw->ip);
				free(dtw);
				return NULL;
		}
	}
	return dtw;
}

/* Delete TLS writer object */
static void
tls_writer_delete(struct dt_tls_writer* dtw)
{
	if(!dtw)
		return;
	if(dtw->ssl)
		SSL_shutdown(dtw->ssl);
	SSL_free(dtw->ssl);
	dtw->ssl = NULL;
	SSL_CTX_free(dtw->ctx);
	if(dtw->fd != -1) {
		close(dtw->fd);
		dtw->fd = -1;
	}
	free(dtw->ip);
	free(dtw->tls_server_name);
	free(dtw);
}

/* The fstrm writer destroy callback for TLS */
static fstrm_res
dt_tls_writer_destroy(void* obj)
{
	struct dt_tls_writer* dtw = (struct dt_tls_writer*)obj;
	tls_writer_delete(dtw);
	return fstrm_res_success;
}

/* The fstrm writer open callback for TLS */
static fstrm_res
dt_tls_writer_open(void* obj)
{
	struct sockaddr_storage addr;
	socklen_t addrlen;
	char* svr, *at = NULL;
	int port = 3333;
	int addrfamily;
	struct dt_tls_writer* dtw = (struct dt_tls_writer*)obj;
	X509* x;

	/* skip action if already connected */
	if(dtw->connected)
		return fstrm_res_success;

	/* figure out port number */
	svr = dtw->ip;
	at = strchr(svr, '@');
	if(at != NULL) {
		*at = 0;
		port = atoi(at+1);
	}

	/* parse addr */
	memset(&addr, 0, sizeof(addr));
#ifdef INET6
	if(strchr(svr, ':')) {
		struct sockaddr_in6 sa;
		addrlen = (socklen_t)sizeof(struct sockaddr_in6);
		memset(&sa, 0, addrlen);
		sa.sin6_family = AF_INET6;
		sa.sin6_port = (in_port_t)htons((uint16_t)port);
		if(inet_pton((int)sa.sin6_family, svr, &sa.sin6_addr) <= 0) {
			log_msg(LOG_ERR, "dnstap: could not parse IP: %s", svr);
			if(at != NULL)
				*at = '@';
			return fstrm_res_failure;
		}
		memcpy(&addr, &sa, addrlen);
		addrfamily = AF_INET6;
	} else
#else
		if(1)
#endif
	{
		struct sockaddr_in sa;
		addrlen = (socklen_t)sizeof(struct sockaddr_in);
		memset(&sa, 0, addrlen);
		sa.sin_family = AF_INET;
		sa.sin_port = (in_port_t)htons((uint16_t)port);
		if(inet_pton((int)sa.sin_family, svr, &sa.sin_addr) <= 0) {
			log_msg(LOG_ERR, "dnstap: could not parse IP: %s", svr);
			if(at != NULL)
				*at = '@';
			return fstrm_res_failure;
		}
		memcpy(&addr, &sa, addrlen);
		addrfamily = AF_INET;
	}
	if(at != NULL)
		*at = '@';

	/* open socket */
	dtw->fd = socket(addrfamily, SOCK_STREAM, 0);
	if(dtw->fd == -1) {
		log_msg(LOG_ERR, "dnstap: socket failed: %s", strerror(errno));
		return fstrm_res_failure;
	}
	if(connect(dtw->fd, (struct sockaddr*)&addr, addrlen) < 0) {
		log_msg(LOG_ERR, "dnstap: connect failed: %s", strerror(errno));
		return fstrm_res_failure;
	}
	dtw->connected = 1;

	/* setup SSL */
	dtw->ssl = SSL_new(dtw->ctx);
	if(!dtw->ssl) {
		log_msg(LOG_ERR, "dnstap: SSL_new failed");
		return fstrm_res_failure;
	}
	SSL_set_connect_state(dtw->ssl);
	(void)SSL_set_mode(dtw->ssl, SSL_MODE_AUTO_RETRY);
	if(!SSL_set_fd(dtw->ssl, dtw->fd)) {
		log_msg(LOG_ERR, "dnstap: SSL_set_fd failed");
		return fstrm_res_failure;
	}
	if(dtw->tls_server_name && dtw->tls_server_name[0]) {
		if(!SSL_set1_host(dtw->ssl, dtw->tls_server_name)) {
			log_msg(LOG_ERR, "dnstap: TLS setting of hostname %s failed to %s",
				dtw->tls_server_name, dtw->ip);
			return fstrm_res_failure;
		}
	}

	/* handshake */
	while(1) {
		int r;
		ERR_clear_error();
		if( (r=SSL_do_handshake(dtw->ssl)) == 1)
			break;
		r = SSL_get_error(dtw->ssl, r);
		if(r != SSL_ERROR_WANT_READ && r != SSL_ERROR_WANT_WRITE) {
			if(r == SSL_ERROR_ZERO_RETURN) {
				log_msg(LOG_ERR, "dnstap: EOF on SSL_do_handshake");
				return fstrm_res_failure;
			}
			if(r == SSL_ERROR_SYSCALL) {
				log_msg(LOG_ERR, "dnstap: SSL_do_handshake failed: %s", strerror(errno));
				return fstrm_res_failure;
			}
			log_crypto_err("dnstap: SSL_do_handshake failed");
			return fstrm_res_failure;
		}
		/* wants to be called again */
	}

	/* check authenticity of server */
	if(SSL_get_verify_result(dtw->ssl) != X509_V_OK) {
		log_crypto_err("SSL verification failed");
		return fstrm_res_failure;
	}
#ifdef HAVE_SSL_GET1_PEER_CERTIFICATE
	x = SSL_get1_peer_certificate(dtw->ssl);
#else
	x = SSL_get_peer_certificate(dtw->ssl);
#endif
	if(!x) {
		log_crypto_err("Server presented no peer certificate");
		return fstrm_res_failure;
	}
	X509_free(x);

	return fstrm_res_success;
}

/* The fstrm writer close callback for TLS */
static fstrm_res
dt_tls_writer_close(void* obj)
{
	struct dt_tls_writer* dtw = (struct dt_tls_writer*)obj;
	if(dtw->connected) {
		dtw->connected = 0;
		if(dtw->ssl)
			SSL_shutdown(dtw->ssl);
		SSL_free(dtw->ssl);
		dtw->ssl = NULL;
		if(dtw->fd != -1) {
			close(dtw->fd);
			dtw->fd = -1;
		}
		return fstrm_res_success;
	}
	return fstrm_res_failure;
}

/* The fstrm writer read callback for TLS */
static fstrm_res
dt_tls_writer_read(void* obj, void* buf, size_t nbytes)
{
	/* want to read nbytes of data */
	struct dt_tls_writer* dtw = (struct dt_tls_writer*)obj;
	size_t nread = 0;
	if(!dtw->connected)
		return fstrm_res_failure;
	while(nread < nbytes) {
		int r;
		ERR_clear_error();
		if((r = SSL_read(dtw->ssl, ((char*)buf)+nread, nbytes-nread)) <= 0) {
			r = SSL_get_error(dtw->ssl, r);
			if(r == SSL_ERROR_ZERO_RETURN) {
				log_msg(LOG_ERR, "dnstap: EOF from %s",
					dtw->ip);
				return fstrm_res_failure;
			}
			if(r == SSL_ERROR_SYSCALL) {
				log_msg(LOG_ERR, "dnstap: read %s: %s",
					dtw->ip, strerror(errno));
				return fstrm_res_failure;
			}
			if(r == SSL_ERROR_SSL) {
				log_crypto_err("dnstap: could not SSL_read");
				return fstrm_res_failure;
			}
			log_msg(LOG_ERR, "dnstap: SSL_read failed with err %d",
				r);
			return fstrm_res_failure;
		}
		nread += r;
	}
	return fstrm_res_success;
}

/* The fstrm writer write callback for TLS */
static fstrm_res
dt_tls_writer_write(void* obj, const struct iovec* iov, int iovcnt)
{
	struct dt_tls_writer* dtw = (struct dt_tls_writer*)obj;
	int i;
	if(!dtw->connected)
		return fstrm_res_failure;
	for(i=0; i<iovcnt; i++) {
		if(SSL_write(dtw->ssl, iov[i].iov_base, (int)(iov[i].iov_len)) <= 0) {
			log_crypto_err("dnstap: could not SSL_write");
			return fstrm_res_failure;
		}
	}
	return fstrm_res_success;
}

/* Create the fstrm writer object for TLS */
static struct fstrm_writer*
dt_tls_make_writer(struct fstrm_writer_options* fwopt,
	struct dt_tls_writer* dtw)
{
	struct fstrm_rdwr* rdwr = fstrm_rdwr_init(dtw);
	fstrm_rdwr_set_destroy(rdwr, dt_tls_writer_destroy);
	fstrm_rdwr_set_open(rdwr, dt_tls_writer_open);
	fstrm_rdwr_set_close(rdwr, dt_tls_writer_close);
	fstrm_rdwr_set_read(rdwr, dt_tls_writer_read);
	fstrm_rdwr_set_write(rdwr, dt_tls_writer_write);
	return fstrm_writer_init(fwopt, &rdwr);
}
#endif /* HAVE_SSL */

/* check that the socket file can be opened and exists, print error if not */
static void
check_socket_file(const char* socket_path)
{
	struct stat statbuf;
	memset(&statbuf, 0, sizeof(statbuf));
	if(stat(socket_path, &statbuf) < 0) {
		log_msg(LOG_WARNING, "could not open dnstap-socket-path: %s, %s",
			socket_path, strerror(errno));
	}
}

struct dt_env *
dt_create(const char *socket_path, char* ip, unsigned num_workers,
	int tls, char* tls_server_name, char* tls_cert_bundle,
	char* tls_client_key_file, char* tls_client_cert_file)
{
#ifndef NDEBUG
	fstrm_res res;
#endif
	struct dt_env *env;
	struct fstrm_iothr_options *fopt;
	struct fstrm_unix_writer_options *fuwopt = NULL;
	struct fstrm_tcp_writer_options *ftwopt = NULL;
	struct fstrm_writer *fw;
	struct fstrm_writer_options *fwopt;

	assert(num_workers > 0);
	if(ip == NULL || ip[0] == 0) {
		VERBOSITY(1, (LOG_INFO, "attempting to connect to dnstap socket %s",
			socket_path));
		assert(socket_path != NULL);
		check_socket_file(socket_path);
	} else {
		VERBOSITY(1, (LOG_INFO, "attempting to connect to dnstap %ssocket %s",
			(tls?"tls ":""), ip));
	}

	env = (struct dt_env *) calloc(1, sizeof(struct dt_env));
	if (!env)
		return NULL;

	fwopt = fstrm_writer_options_init();
#ifndef NDEBUG
	res = 
#else
	(void)
#endif
	    fstrm_writer_options_add_content_type(fwopt,
		DNSTAP_CONTENT_TYPE, sizeof(DNSTAP_CONTENT_TYPE) - 1);
	assert(res == fstrm_res_success);

	if(ip == NULL || ip[0] == 0) {
		fuwopt = fstrm_unix_writer_options_init();
		fstrm_unix_writer_options_set_socket_path(fuwopt, socket_path);
	} else {
		char* at = strchr(ip, '@');
		if(!tls) {
			ftwopt = fstrm_tcp_writer_options_init();
			if(at == NULL) {
				fstrm_tcp_writer_options_set_socket_address(ftwopt, ip);
				fstrm_tcp_writer_options_set_socket_port(ftwopt, "3333");
			} else {
				*at = 0;
				fstrm_tcp_writer_options_set_socket_address(ftwopt, ip);
				fstrm_tcp_writer_options_set_socket_port(ftwopt, at+1);
				*at = '@';
			}
		} else {
#ifdef HAVE_SSL
			env->tls_writer = tls_writer_init(ip, tls_server_name,
				tls_cert_bundle, tls_client_key_file,
				tls_client_cert_file);
#else
			(void)tls_server_name;
			(void)tls_cert_bundle;
			(void)tls_client_key_file;
			(void)tls_client_cert_file;
			log_msg(LOG_ERR, "dnstap: tls enabled but compiled without ssl.");
#endif
			if(!env->tls_writer) {
				log_msg(LOG_ERR, "dt_create: tls_writer_init() failed");
				fstrm_writer_options_destroy(&fwopt);
				free(env);
				return NULL;
			}
		}
	}
	if(ip == NULL || ip[0] == 0)
		fw = fstrm_unix_writer_init(fuwopt, fwopt);
	else if(!tls)
		fw = fstrm_tcp_writer_init(ftwopt, fwopt);
#ifdef HAVE_SSL
	else
		fw = dt_tls_make_writer(fwopt, env->tls_writer);
#endif
	assert(fw != NULL);

	fopt = fstrm_iothr_options_init();
	fstrm_iothr_options_set_num_input_queues(fopt, num_workers);
	env->iothr = fstrm_iothr_init(fopt, &fw);
	if (env->iothr == NULL) {
		log_msg(LOG_ERR, "dt_create: fstrm_iothr_init() failed");
		fstrm_writer_destroy(&fw);
		free(env);
		env = NULL;
	}
	fstrm_iothr_options_destroy(&fopt);

	if(ip == NULL || ip[0] == 0)
		fstrm_unix_writer_options_destroy(&fuwopt);
	else if(!tls)
		fstrm_tcp_writer_options_destroy(&ftwopt);
	fstrm_writer_options_destroy(&fwopt);

	return env;
}

static void
dt_apply_identity(struct dt_env *env, struct nsd_options *cfg)
{
	char buf[MAXHOSTNAMELEN+1];
	if (!cfg->dnstap_send_identity)
		return;
	free(env->identity);
	if (cfg->dnstap_identity == NULL || cfg->dnstap_identity[0] == 0) {
		if (gethostname(buf, MAXHOSTNAMELEN) == 0) {
			buf[MAXHOSTNAMELEN] = 0;
			env->identity = strdup(buf);
		} else {
			error("dt_apply_identity: gethostname() failed");
		}
	} else {
		env->identity = strdup(cfg->dnstap_identity);
	}
	if (env->identity == NULL)
		error("dt_apply_identity: strdup() failed");
	env->len_identity = (unsigned int)strlen(env->identity);
	VERBOSITY(1, (LOG_INFO, "dnstap identity field set to \"%s\"",
		env->identity));
}

static void
dt_apply_version(struct dt_env *env, struct nsd_options *cfg)
{
	if (!cfg->dnstap_send_version)
		return;
	free(env->version);
	if (cfg->dnstap_version == NULL || cfg->dnstap_version[0] == 0)
		env->version = strdup(PACKAGE_STRING);
	else
		env->version = strdup(cfg->dnstap_version);
	if (env->version == NULL)
		error("dt_apply_version: strdup() failed");
	env->len_version = (unsigned int)strlen(env->version);
	VERBOSITY(1, (LOG_INFO, "dnstap version field set to \"%s\"",
		env->version));
}

void
dt_apply_cfg(struct dt_env *env, struct nsd_options *cfg)
{
	if (!cfg->dnstap_enable)
		return;

	dt_apply_identity(env, cfg);
	dt_apply_version(env, cfg);
	if ((env->log_auth_query_messages = (unsigned int)
	     cfg->dnstap_log_auth_query_messages))
	{
		VERBOSITY(1, (LOG_INFO, "dnstap Message/AUTH_QUERY enabled"));
	}
	if ((env->log_auth_response_messages = (unsigned int)
	     cfg->dnstap_log_auth_response_messages))
	{
		VERBOSITY(1, (LOG_INFO, "dnstap Message/AUTH_RESPONSE enabled"));
	}
}

int
dt_init(struct dt_env *env)
{
	env->ioq = fstrm_iothr_get_input_queue(env->iothr);
	if (env->ioq == NULL)
		return 0;
	return 1;
}

void
dt_delete(struct dt_env *env)
{
	if (!env)
		return;
	VERBOSITY(1, (LOG_INFO, "closing dnstap socket"));
	fstrm_iothr_destroy(&env->iothr);
	free(env->identity);
	free(env->version);
	free(env);
}

static void
dt_fill_timeval(const struct timeval *tv,
		uint64_t *time_sec, protobuf_c_boolean *has_time_sec,
		uint32_t *time_nsec, protobuf_c_boolean *has_time_nsec)
{
#ifndef S_SPLINT_S
	*time_sec = tv->tv_sec;
	*time_nsec = tv->tv_usec * 1000;
#endif
	*has_time_sec = 1;
	*has_time_nsec = 1;
}

static void
dt_fill_buffer(uint8_t* pkt, size_t pktlen, ProtobufCBinaryData *p, protobuf_c_boolean *has)
{
	p->len = pktlen;
	p->data = pkt;
	*has = 1;
}

static void
dt_msg_fill_net(struct dt_msg *dm,
#ifdef INET6
		struct sockaddr_storage *rs,
		struct sockaddr_storage *qs,
#else
		struct sockaddr_in *rs,
		struct sockaddr_in *qs,
#endif
		int is_tcp,
		ProtobufCBinaryData *raddr, protobuf_c_boolean *has_raddr,
		uint32_t *rport, protobuf_c_boolean *has_rport,
		ProtobufCBinaryData *qaddr, protobuf_c_boolean *has_qaddr,
		uint32_t *qport, protobuf_c_boolean *has_qport)

{
#ifdef INET6
	assert(qs->ss_family == AF_INET6 || qs->ss_family == AF_INET);
	if (qs->ss_family == AF_INET6) {
		struct sockaddr_in6 *s = (struct sockaddr_in6 *) qs;

		/* socket_family */
		dm->m.socket_family = DNSTAP__SOCKET_FAMILY__INET6;
		dm->m.has_socket_family = 1;

		/* addr: query_address or response_address */
		qaddr->data = s->sin6_addr.s6_addr;
		qaddr->len = 16; /* IPv6 */
		*has_qaddr = 1;

		/* port: query_port or response_port */
		*qport = ntohs(s->sin6_port);
		*has_qport = 1;
	} else if (qs->ss_family == AF_INET) {
#else
	if (qs->sin_family == AF_INET) {
#endif /* INET6 */
		struct sockaddr_in *s = (struct sockaddr_in *) qs;

		/* socket_family */
		dm->m.socket_family = DNSTAP__SOCKET_FAMILY__INET;
		dm->m.has_socket_family = 1;

		/* addr: query_address or response_address */
		qaddr->data = (uint8_t *) &s->sin_addr.s_addr;
		qaddr->len = 4; /* IPv4 */
		*has_qaddr = 1;

		/* port: query_port or response_port */
		*qport = ntohs(s->sin_port);
		*has_qport = 1;
	}

#ifdef INET6
        assert(rs->ss_family == AF_INET6 || rs->ss_family == AF_INET);
        if (rs->ss_family == AF_INET6) {
                struct sockaddr_in6 *s = (struct sockaddr_in6 *) rs;

                /* addr: query_address or response_address */
                raddr->data = s->sin6_addr.s6_addr;
                raddr->len = 16; /* IPv6 */
                *has_raddr = 1;

                /* port: query_port or response_port */
                *rport = ntohs(s->sin6_port);
                *has_rport = 1;
        } else if (rs->ss_family == AF_INET) {
#else
        if (rs->sin_family == AF_INET) {
#endif /* INET6 */
                struct sockaddr_in *s = (struct sockaddr_in *) rs;

                /* addr: query_address or response_address */
                raddr->data = (uint8_t *) &s->sin_addr.s_addr;
                raddr->len = 4; /* IPv4 */
                *has_raddr = 1;

                /* port: query_port or response_port */
                *rport = ntohs(s->sin_port);
                *has_rport = 1;
        }


	if (!is_tcp) {
		/* socket_protocol */
		dm->m.socket_protocol = DNSTAP__SOCKET_PROTOCOL__UDP;
		dm->m.has_socket_protocol = 1;
	} else {
		/* socket_protocol */
		dm->m.socket_protocol = DNSTAP__SOCKET_PROTOCOL__TCP;
		dm->m.has_socket_protocol = 1;
	}
}

void
dt_msg_send_auth_query(struct dt_env *env,
#ifdef INET6
	struct sockaddr_storage* local_addr,
	struct sockaddr_storage* addr,
#else
	struct sockaddr_in* local_addr,
	struct sockaddr_in* addr,
#endif
	int is_tcp, uint8_t* zone, size_t zonelen, uint8_t* pkt, size_t pktlen)
{
	struct dt_msg dm;
	struct timeval qtime;

	gettimeofday(&qtime, NULL);

	/* type */
	dt_msg_init(env, &dm, DNSTAP__MESSAGE__TYPE__AUTH_QUERY);

	if(zone) {
		/* query_zone */
		dm.m.query_zone.data = zone;
		dm.m.query_zone.len = zonelen;
		dm.m.has_query_zone = 1;
	}

	/* query_time */
	dt_fill_timeval(&qtime,
			&dm.m.query_time_sec, &dm.m.has_query_time_sec,
			&dm.m.query_time_nsec, &dm.m.has_query_time_nsec);

	/* query_message */
	dt_fill_buffer(pkt, pktlen, &dm.m.query_message, &dm.m.has_query_message);

	/* socket_family, socket_protocol, query_address, query_port, reponse_address (local_address), response_port (local_port) */
	dt_msg_fill_net(&dm, local_addr, addr, is_tcp,
			&dm.m.response_address, &dm.m.has_response_address,
			&dm.m.response_port, &dm.m.has_response_port,
			&dm.m.query_address, &dm.m.has_query_address,
			&dm.m.query_port, &dm.m.has_query_port);


	if (dt_pack(&dm.d, &dm.buf, &dm.len_buf))
		dt_send(env, dm.buf, dm.len_buf);
}

void
dt_msg_send_auth_response(struct dt_env *env,
#ifdef INET6
	struct sockaddr_storage* local_addr,
	struct sockaddr_storage* addr,
#else
	struct sockaddr_in* local_addr,
	struct sockaddr_in* addr,
#endif
	int is_tcp, uint8_t* zone, size_t zonelen, uint8_t* pkt, size_t pktlen)
{
	struct dt_msg dm;
	struct timeval rtime;

	gettimeofday(&rtime, NULL);

	/* type */
	dt_msg_init(env, &dm, DNSTAP__MESSAGE__TYPE__AUTH_RESPONSE);

	if(zone) {
		/* query_zone */
		dm.m.query_zone.data = zone;
		dm.m.query_zone.len = zonelen;
		dm.m.has_query_zone = 1;
	}

	/* response_time */
	dt_fill_timeval(&rtime,
			&dm.m.response_time_sec, &dm.m.has_response_time_sec,
			&dm.m.response_time_nsec, &dm.m.has_response_time_nsec);

	/* response_message */
	dt_fill_buffer(pkt, pktlen, &dm.m.response_message, &dm.m.has_response_message);

	/* socket_family, socket_protocol, query_address, query_port, response_address (local_address), response_port (local_port)  */
	dt_msg_fill_net(&dm, local_addr, addr, is_tcp,
			&dm.m.response_address, &dm.m.has_response_address,
			&dm.m.response_port, &dm.m.has_response_port,
			&dm.m.query_address, &dm.m.has_query_address,
			&dm.m.query_port, &dm.m.has_query_port);

	if (dt_pack(&dm.d, &dm.buf, &dm.len_buf))
		dt_send(env, dm.buf, dm.len_buf);
}

#endif /* USE_DNSTAP */
