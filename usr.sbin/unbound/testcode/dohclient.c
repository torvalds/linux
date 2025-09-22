/*
 * testcode/dohclient.c - debug program. Perform multiple DNS queries using DoH.
 *
 * Copyright (c) 2020, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** 
 * \file
 *
 * Simple DNS-over-HTTPS client. For testing and debugging purposes.
 * No authentication of TLS cert.
 */

#include "config.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#include "sldns/wire2str.h"
#include "sldns/sbuffer.h"
#include "sldns/str2wire.h"
#include "sldns/parseutil.h"
#include "util/data/msgencode.h"
#include "util/data/msgreply.h"
#include "util/data/msgparse.h"
#include "util/net_help.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#ifdef HAVE_NGHTTP2
#include <nghttp2/nghttp2.h>

struct http2_session {
	nghttp2_session* session;
	SSL* ssl;
	int fd;
	int query_count;
	/* Use POST :method if 1 */
	int post;
	int block_select;
	const char* authority;
	const char* endpoint;
	const char* content_type;
};

struct http2_stream {
	int32_t stream_id;
	int res_status;
	struct sldns_buffer* buf;
	char* path;
};

static void usage(char* argv[])
{
	printf("usage: %s [options] name type class ...\n", argv[0]);
	printf("	sends the name-type-class queries over "
			"DNS-over-HTTPS.\n");
	printf("-s server	IP address to send the queries to, "
			"default: 127.0.0.1\n");
	printf("-p		Port to connect to, default: %d\n",
		UNBOUND_DNS_OVER_HTTPS_PORT);
	printf("-P		Use POST method instead of default GET\n");
	printf("-e		HTTP endpoint, default: /dns-query\n");
	printf("-c		Content-type in request, default: "
		"application/dns-message\n");
	printf("-n		no-tls, TLS is disabled\n");
	printf("-h 		This help text\n");
	exit(1);
}

/** open TCP socket to svr */
static int
open_svr(const char* svr, int port)
{
	struct sockaddr_storage addr;
	socklen_t addrlen;
	int fd = -1;
	int r;
	if(!ipstrtoaddr(svr, port, &addr, &addrlen)) {
		printf("fatal: bad server specs '%s'\n", svr);
		exit(1);
	}

	fd = socket(addr_is_ip6(&addr, addrlen)?PF_INET6:PF_INET,
		SOCK_STREAM, 0);
	if(fd == -1) {
		perror("socket() error");
		exit(1);
	}
	r = connect(fd, (struct sockaddr*)&addr, addrlen);
	if(r < 0 && r != EINPROGRESS) {
		perror("connect() error");
		exit(1);
	}
	return fd;
}

static ssize_t http2_submit_request_read_cb(
	nghttp2_session* ATTR_UNUSED(session),
	int32_t ATTR_UNUSED(stream_id), uint8_t* buf, size_t length,
	uint32_t* data_flags, nghttp2_data_source* source,
	void* ATTR_UNUSED(cb_arg))
{
	if(length > sldns_buffer_remaining(source->ptr))
		length = sldns_buffer_remaining(source->ptr);

	memcpy(buf, sldns_buffer_current(source->ptr), length);
	sldns_buffer_skip(source->ptr, length);

	if(sldns_buffer_remaining(source->ptr) == 0) {
		*data_flags |= NGHTTP2_DATA_FLAG_EOF;
	}

	return length;
}

static void
submit_query(struct http2_session* h2_session, struct sldns_buffer* buf)
{
	int32_t stream_id;
	struct http2_stream* h2_stream;
	nghttp2_nv headers[5];
	char* qb64;
	size_t qb64_size;
	size_t qb64_expected_size;
	size_t i;
	nghttp2_data_provider data_prd;

	h2_stream = calloc(1,  sizeof(*h2_stream));
	if(!h2_stream)
		fatal_exit("could not malloc http2 stream");
	h2_stream->buf = buf;

	if(h2_session->post) {
		data_prd.source.ptr = buf;
		data_prd.read_callback = http2_submit_request_read_cb;
		h2_stream->path = (char*)h2_session->endpoint;
	} else {
		qb64_expected_size = sldns_b64_ntop_calculate_size(
				sldns_buffer_remaining(buf));
		qb64 = malloc(qb64_expected_size);
		if(!qb64) fatal_exit("out of memory");
		qb64_size = sldns_b64url_ntop(sldns_buffer_begin(buf),
			sldns_buffer_remaining(buf), qb64, qb64_expected_size);
		h2_stream->path = malloc(strlen(
			h2_session->endpoint)+strlen("?dns=")+qb64_size+1);
		if(!h2_stream->path) fatal_exit("out of memory");
		snprintf(h2_stream->path, strlen(h2_session->endpoint)+
			strlen("?dns=")+qb64_size+1, "%s?dns=%s",
			h2_session->endpoint, qb64);
		free(qb64);
	}

	headers[0].name = (uint8_t*)":method";
	if(h2_session->post)
		headers[0].value = (uint8_t*)"POST";
	else
		headers[0].value = (uint8_t*)"GET";
	headers[1].name = (uint8_t*)":path";
	headers[1].value = (uint8_t*)h2_stream->path;
	headers[2].name = (uint8_t*)":scheme";
	if(h2_session->ssl)
		headers[2].value = (uint8_t*)"https";
	else
		headers[2].value = (uint8_t*)"http";
	headers[3].name = (uint8_t*)":authority";
	headers[3].value = (uint8_t*)h2_session->authority;
	headers[4].name = (uint8_t*)"content-type";
	headers[4].value = (uint8_t*)h2_session->content_type;

	printf("Request headers\n");
	for(i=0; i<sizeof(headers)/sizeof(headers[0]); i++) {
		headers[i].namelen = strlen((char*)headers[i].name);
		headers[i].valuelen = strlen((char*)headers[i].value);
		headers[i].flags = NGHTTP2_NV_FLAG_NONE;
		printf("%s: %s\n", headers[i].name, headers[i].value);
	}

	stream_id = nghttp2_submit_request(h2_session->session, NULL, headers,
		sizeof(headers)/sizeof(headers[0]),
		(h2_session->post) ? &data_prd : NULL, h2_stream);
	if(stream_id < 0) {
		printf("Failed to submit nghttp2 request");
		exit(1);
	}
	h2_session->query_count++;
	h2_stream->stream_id = stream_id;
}

static sldns_buffer*
make_query(char* qname, char* qtype, char* qclass)
{
	struct query_info qinfo;
	struct edns_data edns;
	sldns_buffer* buf = sldns_buffer_new(65553);
	if(!buf) fatal_exit("out of memory");
	qinfo.qname = sldns_str2wire_dname(qname, &qinfo.qname_len);
	if(!qinfo.qname) {
		printf("cannot parse query name: '%s'\n", qname);
		exit(1);
	}
	qinfo.qtype = sldns_get_rr_type_by_name(qtype);
	if(qinfo.qtype == 0 && strcmp(qtype, "TYPE0") != 0) {
		printf("cannot parse query type: '%s'\n", qtype);
		exit(1);
	}
	qinfo.qclass = sldns_get_rr_class_by_name(qclass);
	if(qinfo.qclass == 0 && strcmp(qclass, "CLASS0") != 0) {
		printf("cannot parse query class: '%s'\n", qclass);
		exit(1);
	}
	qinfo.local_alias = NULL;

	qinfo_query_encode(buf, &qinfo); /* flips buffer */
	free(qinfo.qname);
	sldns_buffer_write_u16_at(buf, 0, 0x0000);
	sldns_buffer_write_u16_at(buf, 2, BIT_RD);
	memset(&edns, 0, sizeof(edns));
	edns.edns_present = 1;
	edns.bits = EDNS_DO;
	edns.udp_size = 4096;
	if(sldns_buffer_capacity(buf) >=
		sldns_buffer_limit(buf)+calc_edns_field_size(&edns))
		attach_edns_record(buf, &edns);
	return buf;
}

static ssize_t http2_recv_cb(nghttp2_session* ATTR_UNUSED(session),
	uint8_t* buf, size_t len, int ATTR_UNUSED(flags), void* cb_arg)
{
	struct http2_session* h2_session = (struct http2_session*)cb_arg;
	int r;
	ssize_t ret;
	struct timeval tv, *waittv;
	fd_set rfd;
	ERR_clear_error();

	memset(&tv, 0, sizeof(tv));

	if(h2_session->block_select && h2_session->query_count <= 0) {
		return NGHTTP2_ERR_WOULDBLOCK;
	}
	if(h2_session->block_select)
		waittv = NULL;
	else
		waittv = &tv;
	memset(&rfd, 0, sizeof(rfd));
	FD_ZERO(&rfd);
	FD_SET(h2_session->fd, &rfd);
	r = select(h2_session->fd+1, &rfd, NULL, NULL, waittv);
	if(r <= 0) {
		return NGHTTP2_ERR_WOULDBLOCK;
	}

	if(h2_session->ssl) {
		r = SSL_read(h2_session->ssl, buf, len);
		if(r <= 0) {
			int want = SSL_get_error(h2_session->ssl, r);
			if(want == SSL_ERROR_ZERO_RETURN) {
				return NGHTTP2_ERR_EOF;
			}
			log_crypto_err_io("could not SSL_read", want);
			return NGHTTP2_ERR_EOF;
		}
		return r;
	}

	ret = read(h2_session->fd, buf, len);
	if(ret == 0) {
		return NGHTTP2_ERR_EOF;
	} else if(ret < 0) {
		log_err("could not http2 read: %s", strerror(errno));
		return NGHTTP2_ERR_EOF;
	}
	return ret;
}

static ssize_t http2_send_cb(nghttp2_session* ATTR_UNUSED(session),
	const uint8_t* buf, size_t len, int ATTR_UNUSED(flags), void* cb_arg)
{
	struct http2_session* h2_session = (struct http2_session*)cb_arg;
	ssize_t ret;

	if(h2_session->ssl) {
		int r;
		ERR_clear_error();
		r = SSL_write(h2_session->ssl, buf, len);
		if(r <= 0) {
			int want = SSL_get_error(h2_session->ssl, r);
			if(want == SSL_ERROR_ZERO_RETURN) {
				return NGHTTP2_ERR_CALLBACK_FAILURE;
			}
			log_crypto_err_io("could not SSL_write", want);
			return NGHTTP2_ERR_CALLBACK_FAILURE;
		}
		return r;
	}

	ret = write(h2_session->fd, buf, len);
	if(ret == 0) {
		return NGHTTP2_ERR_CALLBACK_FAILURE;
	} else if(ret < 0) {
		log_err("could not http2 write: %s", strerror(errno));
		return NGHTTP2_ERR_CALLBACK_FAILURE;
	}
	return ret;
}

static int http2_stream_close_cb(nghttp2_session* ATTR_UNUSED(session),
	int32_t ATTR_UNUSED(stream_id),
	nghttp2_error_code ATTR_UNUSED(error_code), void *cb_arg)
{
	struct http2_session* h2_session = (struct http2_session*)cb_arg;
	struct http2_stream* h2_stream;
	if(!(h2_stream = nghttp2_session_get_stream_user_data(
		h2_session->session, stream_id))) {
		return 0;
	}
	h2_session->query_count--;
	sldns_buffer_free(h2_stream->buf);
	if(!h2_session->post)
		free(h2_stream->path);
	free(h2_stream);
	h2_stream = NULL;
	return 0;
}

static int http2_data_chunk_recv_cb(nghttp2_session* ATTR_UNUSED(session),
	uint8_t ATTR_UNUSED(flags), int32_t stream_id, const uint8_t* data,
	size_t len, void* cb_arg)
{
	struct http2_session* h2_session = (struct http2_session*)cb_arg;
	struct http2_stream* h2_stream;

	if(!(h2_stream = nghttp2_session_get_stream_user_data(
		h2_session->session, stream_id))) {
		return 0;
	}

	if(sldns_buffer_remaining(h2_stream->buf) < len) {
		log_err("received data chunk does not fit into buffer");
		return NGHTTP2_ERR_CALLBACK_FAILURE;
	}

	sldns_buffer_write(h2_stream->buf, data, len);

	return 0;
}

static int http2_frame_recv_cb(nghttp2_session *session,
	const nghttp2_frame *frame, void* ATTR_UNUSED(cb_arg))
{
	struct http2_stream* h2_stream;

	if(!(h2_stream = nghttp2_session_get_stream_user_data(
		session, frame->hd.stream_id)))
		return 0;
	if(frame->hd.type == NGHTTP2_HEADERS &&
		frame->headers.cat == NGHTTP2_HCAT_RESPONSE) {
		sldns_buffer_clear(h2_stream->buf);
	}
	if(((frame->hd.type != NGHTTP2_DATA &&
		frame->hd.type != NGHTTP2_HEADERS) ||
		frame->hd.flags & NGHTTP2_FLAG_END_STREAM) &&
			h2_stream->res_status == 200) {
			char* pktstr;
			sldns_buffer_flip(h2_stream->buf);
			pktstr = sldns_wire2str_pkt(
				sldns_buffer_begin(h2_stream->buf),
				sldns_buffer_limit(h2_stream->buf));
			printf("%s\n", pktstr);
			free(pktstr);
			return 0;
	}
	return 0;
}
static int http2_header_cb(nghttp2_session* ATTR_UNUSED(session),
	const nghttp2_frame* frame, const uint8_t* name, size_t namelen,
	const uint8_t* value, size_t ATTR_UNUSED(valuelen),
	uint8_t ATTR_UNUSED(flags), void* cb_arg)
{
	struct http2_stream* h2_stream;
	struct http2_session* h2_session = (struct http2_session*)cb_arg;
	printf("%s %s\n", name, value);
	if(namelen == 7 && memcmp(":status", name, namelen) == 0) {
		if(!(h2_stream = nghttp2_session_get_stream_user_data(
			h2_session->session, frame->hd.stream_id))) {
			return 0;
		}
		h2_stream->res_status = atoi((char*)value);
	}
	return 0;
}

static struct http2_session*
http2_session_create()
{
	struct http2_session* h2_session = calloc(1,
		sizeof(struct http2_session));
	nghttp2_session_callbacks* callbacks;
	if(!h2_session)
		fatal_exit("out of memory");

	if(nghttp2_session_callbacks_new(&callbacks) == NGHTTP2_ERR_NOMEM) {
		log_err("failed to initialize nghttp2 callback");
		free(h2_session);
		return NULL;
	}
	nghttp2_session_callbacks_set_recv_callback(callbacks, http2_recv_cb);
	nghttp2_session_callbacks_set_send_callback(callbacks, http2_send_cb);
	nghttp2_session_callbacks_set_on_stream_close_callback(callbacks,
		http2_stream_close_cb);
	nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks,
		http2_data_chunk_recv_cb);
	nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks,
		http2_frame_recv_cb);
	nghttp2_session_callbacks_set_on_header_callback(callbacks,
		http2_header_cb);
	nghttp2_session_client_new(&h2_session->session, callbacks, h2_session);
	nghttp2_session_callbacks_del(callbacks);
	return h2_session;
}

static void
http2_session_delete(struct http2_session* h2_session)
{
	nghttp2_session_del(h2_session->session);
	free(h2_session);
}

static void
http2_submit_setting(struct http2_session* h2_session)
{
	int ret;
	nghttp2_settings_entry settings[1] = {
		{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
		 100}};

	ret = nghttp2_submit_settings(h2_session->session, NGHTTP2_FLAG_NONE,
		settings, 1);
	if(ret) {
		printf("http2: submit_settings failed, "
			"error: %s\n", nghttp2_strerror(ret));
		exit(1);
	}
}

static void
http2_write(struct http2_session* h2_session)
{
	if(nghttp2_session_want_write(h2_session->session)) {
		if(nghttp2_session_send(h2_session->session)) {
			printf("nghttp2 session send failed\n");
			exit(1);
		}
	}
}

static void
http2_read(struct http2_session* h2_session)
{
	if(nghttp2_session_want_read(h2_session->session)) {
		if(nghttp2_session_recv(h2_session->session)) {
			printf("nghttp2 session mem_recv failed\n");
			exit(1);
		}
	}
}

static void
run(struct http2_session* h2_session, int port, int no_tls, int count, char** q)
{
	int i;
	SSL_CTX* ctx = NULL;
	SSL* ssl = NULL;
	int fd;
	struct sldns_buffer* buf = NULL;

	fd = open_svr(h2_session->authority, port);
	h2_session->fd = fd;

	if(!no_tls) {
		ctx = connect_sslctx_create(NULL, NULL, NULL, 0);
		if(!ctx) fatal_exit("cannot create ssl ctx");
#ifdef HAVE_SSL_CTX_SET_ALPN_PROTOS
		SSL_CTX_set_alpn_protos(ctx, (const unsigned char *)"\x02h2", 3);
#endif
		ssl = outgoing_ssl_fd(ctx, fd);
		if(!ssl) {
			printf("cannot create ssl\n");
			exit(1);
		}
		h2_session->ssl = ssl;
		while(1) {
			int r;
			ERR_clear_error();
			if( (r=SSL_do_handshake(ssl)) == 1)
				break;
			r = SSL_get_error(ssl, r);
			if(r != SSL_ERROR_WANT_READ &&
				r != SSL_ERROR_WANT_WRITE) {
				log_crypto_err_io("could not ssl_handshake", r);
				exit(1);
			}
		}
	}

	http2_submit_setting(h2_session);
	http2_write(h2_session);
	http2_read(h2_session); /* Read setting from remote peer */

	h2_session->block_select = 1;

	/* handle query */
	for(i=0; i<count; i+=3) {
		buf = make_query(q[i], q[i+1], q[i+2]);
		submit_query(h2_session, buf);
	}
	http2_write(h2_session);
	while(h2_session->query_count) {
		http2_read(h2_session);
		http2_write(h2_session);
	}

	/* shutdown */
	http2_session_delete(h2_session);
	if(ssl) {
		SSL_shutdown(ssl);
		SSL_free(ssl);
	}
	if(ctx) {
		SSL_CTX_free(ctx);
	}
	sock_close(fd);
}

/** getopt global, in case header files fail to declare it. */
extern int optind;
/** getopt global, in case header files fail to declare it. */
extern char* optarg;
int main(int argc, char** argv)
{
	int c;
	int port = UNBOUND_DNS_OVER_HTTPS_PORT, no_tls = 0;
	struct http2_session* h2_session;

#ifdef USE_WINSOCK
	WSADATA wsa_data;
	if(WSAStartup(MAKEWORD(2,2), &wsa_data) != 0) {
		printf("WSAStartup failed\n");
		return 1;
	}
#endif
	checklock_start();
	log_init(0, 0, 0);
	log_ident_set("dohclient");

	h2_session = http2_session_create();
	if(!h2_session) fatal_exit("out of memory");
	if(argc == 1) {
		usage(argv);
	}
	
	h2_session->authority = "127.0.0.1";
	h2_session->post = 0;
	h2_session->endpoint = "/dns-query";
	h2_session->content_type = "application/dns-message";

	while((c=getopt(argc, argv, "c:e:hns:p:P")) != -1) {
		switch(c) {
			case 'c':
				h2_session->content_type = optarg;
				break;
			case 'e':
				h2_session->endpoint = optarg;
				break;
			case 'n':
				no_tls = 1;
				break;
			case 'p':
				if(atoi(optarg)==0 && strcmp(optarg,"0")!=0) {
					printf("error parsing port, "
					    "number expected: %s\n", optarg);
					return 1;
				}
				port = atoi(optarg);
				break;
			case 'P':
				h2_session->post = 1;
				break;
			case 's':
				h2_session->authority = optarg;
				break;
			case 'h':
			case '?':
			default:
				usage(argv);
		}
	}
	argc -= optind;
	argv += optind;
	if(argc%3!=0) {
		printf("Invalid input. Specify qname, qtype, and qclass.\n");
		return 1;
	}

	if(!no_tls) {
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_SSL)
		ERR_load_SSL_strings();
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_CRYPTO)
#  ifndef S_SPLINT_S
		OpenSSL_add_all_algorithms();
#  endif
#else
		OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS
			| OPENSSL_INIT_ADD_ALL_DIGESTS
			| OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_SSL)
		(void)SSL_library_init();
#else
		(void)OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS, NULL);
#endif
	}
	run(h2_session, port, no_tls, argc, argv);

	checklock_stop();
#ifdef USE_WINSOCK
	WSACleanup();
#endif
	return 0;
}
#else
int main(int ATTR_UNUSED(argc), char** ATTR_UNUSED(argv))
{
	printf("Compiled without nghttp2, cannot run test.\n");
	return 1;
}
#endif /*  HAVE_NGHTTP2 */
