/* $OpenBSD: dtlstest.c,v 1.18 2022/11/26 16:08:56 tb Exp $ */
/*
 * Copyright (c) 2020, 2021 Joel Sing <jsing@openbsd.org>
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

#include <netinet/in.h>
#include <sys/socket.h>

#include <err.h>
#include <limits.h>
#include <poll.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include "bio_local.h"
#include "ssl_local.h"

const char *server_ca_file;
const char *server_cert_file;
const char *server_key_file;

char dtls_cookie[32];

int debug = 0;

void tls12_record_layer_set_initial_epoch(struct tls12_record_layer *rl,
    uint16_t epoch);

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	if (len % 8)
		fprintf(stderr, "\n");
}

#define BIO_C_DELAY_COUNT	1000
#define BIO_C_DELAY_FLUSH	1001
#define BIO_C_DELAY_PACKET	1002
#define BIO_C_DROP_PACKET	1003
#define BIO_C_DROP_RANDOM	1004

struct bio_packet_monkey_ctx {
	unsigned int delay_count;
	unsigned int delay_mask;
	unsigned int drop_rand;
	unsigned int drop_mask;
	uint8_t *delayed_msg;
	size_t delayed_msg_len;
};

static int
bio_packet_monkey_new(BIO *bio)
{
	struct bio_packet_monkey_ctx *ctx;

	if ((ctx = calloc(1, sizeof(*ctx))) == NULL)
		return 0;

	bio->flags = 0;
	bio->init = 1;
	bio->num = 0;
	bio->ptr = ctx;

	return 1;
}

static int
bio_packet_monkey_free(BIO *bio)
{
	struct bio_packet_monkey_ctx *ctx;

	if (bio == NULL)
		return 1;

	ctx = bio->ptr;
	free(ctx->delayed_msg);
	free(ctx);

	return 1;
}

static int
bio_packet_monkey_delay_flush(BIO *bio)
{
	struct bio_packet_monkey_ctx *ctx = bio->ptr;

	if (ctx->delayed_msg == NULL)
		return 1;

	if (debug)
		fprintf(stderr, "DEBUG: flushing delayed packet...\n");
	if (debug > 1)
		hexdump(ctx->delayed_msg, ctx->delayed_msg_len);

	BIO_write(bio->next_bio, ctx->delayed_msg, ctx->delayed_msg_len);

	free(ctx->delayed_msg);
	ctx->delayed_msg = NULL;

	return BIO_ctrl(bio->next_bio, BIO_CTRL_FLUSH, 0, NULL);
}

static long
bio_packet_monkey_ctrl(BIO *bio, int cmd, long num, void *ptr)
{
	struct bio_packet_monkey_ctx *ctx;

	ctx = bio->ptr;

	switch (cmd) {
	case BIO_C_DELAY_COUNT:
		if (num < 1 || num > 31)
			return 0;
		ctx->delay_count = num;
		return 1;

	case BIO_C_DELAY_FLUSH:
		return bio_packet_monkey_delay_flush(bio);

	case BIO_C_DELAY_PACKET:
		if (num < 1 || num > 31)
			return 0;
		ctx->delay_mask |= 1 << ((unsigned int)num - 1);
		return 1;

	case BIO_C_DROP_PACKET:
		if (num < 1 || num > 31)
			return 0;
		ctx->drop_mask |= 1 << ((unsigned int)num - 1);
		return 1;

	case BIO_C_DROP_RANDOM:
		if (num < 0 || (size_t)num > UINT_MAX)
			return 0;
		ctx->drop_rand = (unsigned int)num;
		return 1;
	}

	if (bio->next_bio == NULL)
		return 0;

	return BIO_ctrl(bio->next_bio, cmd, num, ptr);
}

static int
bio_packet_monkey_read(BIO *bio, char *out, int out_len)
{
	struct bio_packet_monkey_ctx *ctx = bio->ptr;
	int ret;

	if (ctx == NULL || bio->next_bio == NULL)
		return 0;

	ret = BIO_read(bio->next_bio, out, out_len);

	if (ret > 0) {
		if (debug)
			fprintf(stderr, "DEBUG: read packet...\n");
		if (debug > 1)
			hexdump(out, ret);
	}

	BIO_clear_retry_flags(bio);
	if (ret <= 0 && BIO_should_retry(bio->next_bio))
		BIO_set_retry_read(bio);

	return ret;
}

static int
bio_packet_monkey_write(BIO *bio, const char *in, int in_len)
{
	struct bio_packet_monkey_ctx *ctx = bio->ptr;
	const char *label = "writing";
	int delay = 0, drop = 0;
	int ret;

	if (ctx == NULL || bio->next_bio == NULL)
		return 0;

	if (ctx->delayed_msg != NULL && ctx->delay_count > 0)
		ctx->delay_count--;

	if (ctx->delayed_msg != NULL && ctx->delay_count == 0) {
		if (debug)
			fprintf(stderr, "DEBUG: writing delayed packet...\n");
		if (debug > 1)
			hexdump(ctx->delayed_msg, ctx->delayed_msg_len);

		ret = BIO_write(bio->next_bio, ctx->delayed_msg,
		    ctx->delayed_msg_len);

		BIO_clear_retry_flags(bio);
		if (ret <= 0 && BIO_should_retry(bio->next_bio)) {
			BIO_set_retry_write(bio);
			return (ret);
		}

		free(ctx->delayed_msg);
		ctx->delayed_msg = NULL;
	}

	if (ctx->delay_mask > 0) {
		delay = ctx->delay_mask & 1;
		ctx->delay_mask >>= 1;
	}
	if (ctx->drop_rand > 0) {
		drop = arc4random_uniform(ctx->drop_rand) == 0;
	} else if (ctx->drop_mask > 0) {
		drop = ctx->drop_mask & 1;
		ctx->drop_mask >>= 1;
	}

	if (delay)
		label = "delaying";
	if (drop)
		label = "dropping";
	if (debug)
		fprintf(stderr, "DEBUG: %s packet...\n", label);
	if (debug > 1)
		hexdump(in, in_len);

	if (drop)
		return in_len;

	if (delay) {
		if (ctx->delayed_msg != NULL)
			return 0;
		if ((ctx->delayed_msg = calloc(1, in_len)) == NULL)
			return 0;
		memcpy(ctx->delayed_msg, in, in_len);
		ctx->delayed_msg_len = in_len;
		return in_len;
	}

	ret = BIO_write(bio->next_bio, in, in_len);

	BIO_clear_retry_flags(bio);
	if (ret <= 0 && BIO_should_retry(bio->next_bio))
		BIO_set_retry_write(bio);

	return ret;
}

static int
bio_packet_monkey_puts(BIO *bio, const char *str)
{
	return bio_packet_monkey_write(bio, str, strlen(str));
}

static const BIO_METHOD bio_packet_monkey = {
	.type = BIO_TYPE_BUFFER,
	.name = "packet monkey",
	.bread = bio_packet_monkey_read,
	.bwrite = bio_packet_monkey_write,
	.bputs = bio_packet_monkey_puts,
	.ctrl = bio_packet_monkey_ctrl,
	.create = bio_packet_monkey_new,
	.destroy = bio_packet_monkey_free
};

static const BIO_METHOD *
BIO_f_packet_monkey(void)
{
	return &bio_packet_monkey;
}

static BIO *
BIO_new_packet_monkey(void)
{
	return BIO_new(BIO_f_packet_monkey());
}

static int
BIO_packet_monkey_delay(BIO *bio, int num, int count)
{
	if (!BIO_ctrl(bio, BIO_C_DELAY_COUNT, count, NULL))
		return 0;

	return BIO_ctrl(bio, BIO_C_DELAY_PACKET, num, NULL);
}

static int
BIO_packet_monkey_delay_flush(BIO *bio)
{
	return BIO_ctrl(bio, BIO_C_DELAY_FLUSH, 0, NULL);
}

static int
BIO_packet_monkey_drop(BIO *bio, int num)
{
	return BIO_ctrl(bio, BIO_C_DROP_PACKET, num, NULL);
}

#if 0
static int
BIO_packet_monkey_drop_random(BIO *bio, int num)
{
	return BIO_ctrl(bio, BIO_C_DROP_RANDOM, num, NULL);
}
#endif

static int
datagram_pair(int *client_sock, int *server_sock,
    struct sockaddr_in *server_sin)
{
	struct sockaddr_in sin;
	socklen_t sock_len;
	int cs = -1, ss = -1;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = 0;
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if ((ss = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
		err(1, "server socket");
	if (bind(ss, (struct sockaddr *)&sin, sizeof(sin)) == -1)
		err(1, "server bind");
	sock_len = sizeof(sin);
	if (getsockname(ss, (struct sockaddr *)&sin, &sock_len) == -1)
		err(1, "server getsockname");

	if ((cs = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
		err(1, "client socket");
	if (connect(cs, (struct sockaddr *)&sin, sizeof(sin)) == -1)
		err(1, "client connect");

	*client_sock = cs;
	*server_sock = ss;
	memcpy(server_sin, &sin, sizeof(sin));

	return 1;
}

static int
poll_timeout(SSL *client, SSL *server)
{
	int client_timeout = 0, server_timeout = 0;
	struct timeval timeout;

	if (DTLSv1_get_timeout(client, &timeout))
		client_timeout = timeout.tv_sec * 1000 + timeout.tv_usec / 1000;

	if (DTLSv1_get_timeout(server, &timeout))
		server_timeout = timeout.tv_sec * 1000 + timeout.tv_usec / 1000;

	if (client_timeout < 10)
		client_timeout = 10;
	if (server_timeout < 10)
		server_timeout = 10;

	/* XXX */
	if (client_timeout <= 0)
		return server_timeout;
	if (client_timeout > 0 && server_timeout <= 0)
		return client_timeout;
	if (client_timeout < server_timeout)
		return client_timeout;

	return server_timeout;
}

static int
dtls_cookie_generate(SSL *ssl, unsigned char *cookie,
    unsigned int *cookie_len)
{
	arc4random_buf(dtls_cookie, sizeof(dtls_cookie));
	memcpy(cookie, dtls_cookie, sizeof(dtls_cookie));
	*cookie_len = sizeof(dtls_cookie);

	return 1;
}

static int
dtls_cookie_verify(SSL *ssl, const unsigned char *cookie,
    unsigned int cookie_len)
{
	return cookie_len == sizeof(dtls_cookie) &&
	    memcmp(cookie, dtls_cookie, sizeof(dtls_cookie)) == 0;
}

static void
dtls_info_callback(const SSL *ssl, int type, int val)
{
	/*
	 * Squeals ahead... remove the bbio from the info callback, so we can
	 * drop specific messages. Ideally this would be an option for the SSL.
	 */
	if (ssl->wbio == ssl->bbio)
		((SSL *)ssl)->wbio = BIO_pop(ssl->wbio);
}

static SSL *
dtls_client(int sock, struct sockaddr_in *server_sin, long mtu)
{
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	BIO *bio = NULL;

	if ((bio = BIO_new_dgram(sock, BIO_NOCLOSE)) == NULL)
		errx(1, "client bio");
	if (!BIO_socket_nbio(sock, 1))
		errx(1, "client nbio");
	if (!BIO_ctrl_set_connected(bio, 1, server_sin))
		errx(1, "client set connected");

	if ((ssl_ctx = SSL_CTX_new(DTLS_method())) == NULL)
		errx(1, "client context");

	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "client ssl");

	SSL_set_bio(ssl, bio, bio);
	bio = NULL;

	if (mtu > 0) {
		SSL_set_options(ssl, SSL_OP_NO_QUERY_MTU);
		SSL_set_mtu(ssl, mtu);
	}

	SSL_CTX_free(ssl_ctx);
	BIO_free(bio);

	return ssl;
}

static SSL *
dtls_server(int sock, long options, long mtu)
{
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	BIO *bio = NULL;

	if ((bio = BIO_new_dgram(sock, BIO_NOCLOSE)) == NULL)
		errx(1, "server bio");
	if (!BIO_socket_nbio(sock, 1))
		errx(1, "server nbio");

	if ((ssl_ctx = SSL_CTX_new(DTLS_method())) == NULL)
		errx(1, "server context");

	SSL_CTX_set_cookie_generate_cb(ssl_ctx, dtls_cookie_generate);
	SSL_CTX_set_cookie_verify_cb(ssl_ctx, dtls_cookie_verify);
	SSL_CTX_set_dh_auto(ssl_ctx, 2);
	SSL_CTX_set_options(ssl_ctx, options);

	if (SSL_CTX_use_certificate_chain_file(ssl_ctx, server_cert_file) != 1) {
		fprintf(stderr, "FAIL: Failed to load server certificate");
		goto failure;
	}
	if (SSL_CTX_use_PrivateKey_file(ssl_ctx, server_key_file,
	    SSL_FILETYPE_PEM) != 1) {
		fprintf(stderr, "FAIL: Failed to load server private key");
		goto failure;
	}

	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "server ssl");

	if (SSL_use_certificate_chain_file(ssl, server_cert_file) != 1) {
		fprintf(stderr, "FAIL: Failed to load server certificate");
		goto failure;
	}
	SSL_set_bio(ssl, bio, bio);
	bio = NULL;

	if (mtu > 0) {
		SSL_set_options(ssl, SSL_OP_NO_QUERY_MTU);
		SSL_set_mtu(ssl, mtu);
	}

 failure:
	SSL_CTX_free(ssl_ctx);
	BIO_free(bio);

	return ssl;
}

static int
ssl_error(SSL *ssl, const char *name, const char *desc, int ssl_ret,
    short *events)
{
	int ssl_err;

	ssl_err = SSL_get_error(ssl, ssl_ret);

	if (ssl_err == SSL_ERROR_WANT_READ) {
		*events = POLLIN;
	} else if (ssl_err == SSL_ERROR_WANT_WRITE) {
		*events = POLLOUT;
	} else if (ssl_err == SSL_ERROR_SYSCALL && errno == 0) {
		/* Yup, this is apparently a thing... */
	} else {
		fprintf(stderr, "FAIL: %s %s failed - ssl err = %d, errno = %d\n",
		    name, desc, ssl_err, errno);
		ERR_print_errors_fp(stderr);
		return 0;
	}

	return 1;
}

static int
do_connect(SSL *ssl, const char *name, int *done, short *events)
{
	int ssl_ret;

	if ((ssl_ret = SSL_connect(ssl)) != 1)
		return ssl_error(ssl, name, "connect", ssl_ret, events);

	fprintf(stderr, "INFO: %s connect done\n", name);
	*done = 1;

	return 1;
}

static int
do_connect_read(SSL *ssl, const char *name, int *done, short *events)
{
	uint8_t buf[2048];
	int ssl_ret;
	int i;

	if ((ssl_ret = SSL_connect(ssl)) != 1)
		return ssl_error(ssl, name, "connect", ssl_ret, events);

	fprintf(stderr, "INFO: %s connect done\n", name);
	*done = 1;

	for (i = 0; i < 3; i++) {
		fprintf(stderr, "INFO: %s reading after connect\n", name);
		if ((ssl_ret = SSL_read(ssl, buf, sizeof(buf))) != 3) {
			fprintf(stderr, "ERROR: %s read failed\n", name);
			return 0;
		}
	}

	return 1;
}

static int
do_connect_shutdown(SSL *ssl, const char *name, int *done, short *events)
{
	uint8_t buf[2048];
	int ssl_ret;

	if ((ssl_ret = SSL_connect(ssl)) != 1)
		return ssl_error(ssl, name, "connect", ssl_ret, events);

	fprintf(stderr, "INFO: %s connect done\n", name);
	*done = 1;

	ssl_ret = SSL_read(ssl, buf, sizeof(buf));
	if (SSL_get_error(ssl, ssl_ret) != SSL_ERROR_ZERO_RETURN) {
		fprintf(stderr, "FAIL: %s did not receive close-notify\n", name);
		return 0;
	}

	fprintf(stderr, "INFO: %s received close-notify\n", name);

	return 1;
}

static int
do_accept(SSL *ssl, const char *name, int *done, short *events)
{
	int ssl_ret;

	if ((ssl_ret = SSL_accept(ssl)) != 1)
		return ssl_error(ssl, name, "accept", ssl_ret, events);

	fprintf(stderr, "INFO: %s accept done\n", name);
	*done = 1;

	return 1;
}

static int
do_accept_write(SSL *ssl, const char *name, int *done, short *events)
{
	int ssl_ret;
	BIO *bio;
	int i;

	if ((ssl_ret = SSL_accept(ssl)) != 1)
		return ssl_error(ssl, name, "accept", ssl_ret, events);

	fprintf(stderr, "INFO: %s accept done\n", name);

	for (i = 0; i < 3; i++) {
		fprintf(stderr, "INFO: %s writing after accept\n", name);
		if ((ssl_ret = SSL_write(ssl, "abc", 3)) != 3) {
			fprintf(stderr, "ERROR: %s write failed\n", name);
			return 0;
		}
	}

	if ((bio = SSL_get_wbio(ssl)) == NULL)
		errx(1, "SSL has NULL bio");

	/* Flush any delayed packets. */
	BIO_packet_monkey_delay_flush(bio);

	*done = 1;
	return 1;
}

static int
do_accept_shutdown(SSL *ssl, const char *name, int *done, short *events)
{
	int ssl_ret;
	BIO *bio;

	if ((ssl_ret = SSL_accept(ssl)) != 1)
		return ssl_error(ssl, name, "accept", ssl_ret, events);

	fprintf(stderr, "INFO: %s accept done\n", name);

	SSL_shutdown(ssl);

	if ((bio = SSL_get_wbio(ssl)) == NULL)
		errx(1, "SSL has NULL bio");

	/* Flush any delayed packets. */
	BIO_packet_monkey_delay_flush(bio);

	*done = 1;
	return 1;
}

static int
do_read(SSL *ssl, const char *name, int *done, short *events)
{
	uint8_t buf[512];
	int ssl_ret;

	if ((ssl_ret = SSL_read(ssl, buf, sizeof(buf))) > 0) {
		fprintf(stderr, "INFO: %s read done\n", name);
		if (debug > 1)
			hexdump(buf, ssl_ret);
		*done = 1;
		return 1;
	}

	return ssl_error(ssl, name, "read", ssl_ret, events);
}

static int
do_write(SSL *ssl, const char *name, int *done, short *events)
{
	const uint8_t buf[] = "Hello, World!\n";
	int ssl_ret;

	if ((ssl_ret = SSL_write(ssl, buf, sizeof(buf))) > 0) {
		fprintf(stderr, "INFO: %s write done\n", name);
		*done = 1;
		return 1;
	}

	return ssl_error(ssl, name, "write", ssl_ret, events);
}

static int
do_shutdown(SSL *ssl, const char *name, int *done, short *events)
{
	int ssl_ret;

	ssl_ret = SSL_shutdown(ssl);
	if (ssl_ret == 1) {
		fprintf(stderr, "INFO: %s shutdown done\n", name);
		*done = 1;
		return 1;
	}
	return ssl_error(ssl, name, "shutdown", ssl_ret, events);
}

typedef int (ssl_func)(SSL *ssl, const char *name, int *done, short *events);

static int
do_client_server_loop(SSL *client, ssl_func *client_func, SSL *server,
    ssl_func *server_func, struct pollfd pfd[2])
{
	int client_done = 0, server_done = 0;
	int i = 0;

	pfd[0].revents = POLLIN;
	pfd[1].revents = POLLIN;

	do {
		if (!client_done) {
			if (debug)
				fprintf(stderr, "DEBUG: client loop\n");
			if (DTLSv1_handle_timeout(client) > 0)
				fprintf(stderr, "INFO: client timeout\n");
			if (!client_func(client, "client", &client_done,
			    &pfd[0].events))
				return 0;
			if (client_done)
				pfd[0].events = 0;
		}
		if (!server_done) {
			if (debug)
				fprintf(stderr, "DEBUG: server loop\n");
			if (DTLSv1_handle_timeout(server) > 0)
				fprintf(stderr, "INFO: server timeout\n");
			if (!server_func(server, "server", &server_done,
			    &pfd[1].events))
				return 0;
			if (server_done)
				pfd[1].events = 0;
		}
		if (poll(pfd, 2, poll_timeout(client, server)) == -1)
			err(1, "poll");

	} while (i++ < 100 && (!client_done || !server_done));

	if (!client_done || !server_done)
		fprintf(stderr, "FAIL: gave up\n");

	return client_done && server_done;
}

#define MAX_PACKET_DELAYS 32
#define MAX_PACKET_DROPS 32

struct dtls_delay {
	uint8_t packet;
	uint8_t count;
};

struct dtls_test {
	const unsigned char *desc;
	long mtu;
	long ssl_options;
	int client_bbio_off;
	int server_bbio_off;
	uint16_t initial_epoch;
	int write_after_accept;
	int shutdown_after_accept;
	struct dtls_delay client_delays[MAX_PACKET_DELAYS];
	struct dtls_delay server_delays[MAX_PACKET_DELAYS];
	uint8_t client_drops[MAX_PACKET_DROPS];
	uint8_t server_drops[MAX_PACKET_DROPS];
};

static const struct dtls_test dtls_tests[] = {
	{
		.desc = "DTLS without cookies",
		.ssl_options = 0,
	},
	{
		.desc = "DTLS without cookies (initial epoch 0xfffe)",
		.ssl_options = 0,
		.initial_epoch = 0xfffe,
	},
	{
		.desc = "DTLS without cookies (initial epoch 0xffff)",
		.ssl_options = 0,
		.initial_epoch = 0xffff,
	},
	{
		.desc = "DTLS with cookies",
		.ssl_options = SSL_OP_COOKIE_EXCHANGE,
	},
	{
		.desc = "DTLS with low MTU",
		.mtu = 256,
		.ssl_options = 0,
	},
	{
		.desc = "DTLS with low MTU and cookies",
		.mtu = 256,
		.ssl_options = SSL_OP_COOKIE_EXCHANGE,
	},
	{
		.desc = "DTLS with dropped server response",
		.ssl_options = 0,
		.server_drops = { 1 },
	},
	{
		.desc = "DTLS with two dropped server responses",
		.ssl_options = 0,
		.server_drops = { 1, 2 },
	},
	{
		.desc = "DTLS with dropped ServerHello",
		.ssl_options = SSL_OP_NO_TICKET,
		.server_bbio_off = 1,
		.server_drops = { 1 },
	},
	{
		.desc = "DTLS with dropped server Certificate",
		.ssl_options = SSL_OP_NO_TICKET,
		.server_bbio_off = 1,
		.server_drops = { 2 },
	},
	{
		.desc = "DTLS with dropped ServerKeyExchange",
		.ssl_options = SSL_OP_NO_TICKET,
		.server_bbio_off = 1,
		.server_drops = { 3 },
	},
	{
		.desc = "DTLS with dropped ServerHelloDone",
		.ssl_options = SSL_OP_NO_TICKET,
		.server_bbio_off = 1,
		.server_drops = { 4 },
	},
#if 0
	/*
	 * These two result in the server accept completing and the
	 * client looping on a timeout. Presumably the server should not
	 * complete until the client Finished is received... this due to
	 * a flaw in the DTLSv1.0 specification, which is addressed in
	 * DTLSv1.2 (see references to "last flight" in RFC 6347 section
	 * 4.2.4). Our DTLS server code still needs to support this.
	 */
	{
		.desc = "DTLS with dropped server CCS",
		.ssl_options = 0,
		.server_bbio_off = 1,
		.server_drops = { 5 },
	},
	{
		.desc = "DTLS with dropped server Finished",
		.ssl_options = 0,
		.server_bbio_off = 1,
		.server_drops = { 6 },
	},
#endif
	{
		.desc = "DTLS with dropped ClientKeyExchange",
		.ssl_options = 0,
		.client_bbio_off = 1,
		.client_drops = { 2 },
	},
	{
		.desc = "DTLS with dropped client CCS",
		.ssl_options = 0,
		.client_bbio_off = 1,
		.client_drops = { 3 },
	},
	{
		.desc = "DTLS with dropped client Finished",
		.ssl_options = 0,
		.client_bbio_off = 1,
		.client_drops = { 4 },
	},
	{
		/* Send CCS after client Finished. */
		.desc = "DTLS with delayed client CCS",
		.ssl_options = 0,
		.client_bbio_off = 1,
		.client_delays = { { 3, 2 } },
	},
	{
		/*
		 * Send CCS after server Finished - note app data will be
		 * dropped if we send the CCS after app data.
		 */
		.desc = "DTLS with delayed server CCS",
		.ssl_options = SSL_OP_NO_TICKET,
		.server_bbio_off = 1,
		.server_delays = { { 5, 2 } },
		.write_after_accept = 1,
	},
	{
		.desc = "DTLS with delayed server CCS (initial epoch 0xfffe)",
		.ssl_options = SSL_OP_NO_TICKET,
		.server_bbio_off = 1,
		.initial_epoch = 0xfffe,
		.server_delays = { { 5, 2 } },
		.write_after_accept = 1,
	},
	{
		.desc = "DTLS with delayed server CCS (initial epoch 0xffff)",
		.ssl_options = SSL_OP_NO_TICKET,
		.server_bbio_off = 1,
		.initial_epoch = 0xffff,
		.server_delays = { { 5, 2 } },
		.write_after_accept = 1,
	},
	{
		/* Send Finished after app data - this is currently buffered. */
		.desc = "DTLS with delayed server Finished",
		.ssl_options = SSL_OP_NO_TICKET,
		.server_bbio_off = 1,
		.server_delays = { { 6, 3 } },
		.write_after_accept = 1,
	},
	{
		/* Send CCS after server finished and close-notify. */
		.desc = "DTLS with delayed server CCS (close-notify)",
		.ssl_options = SSL_OP_NO_TICKET,
		.server_bbio_off = 1,
		.server_delays = { { 5, 3 } },
		.shutdown_after_accept = 1,
	},
};

#define N_DTLS_TESTS (sizeof(dtls_tests) / sizeof(*dtls_tests))

static void
dtlstest_packet_monkey(SSL *ssl, const struct dtls_delay delays[],
    const uint8_t drops[])
{
	BIO *bio_monkey;
	BIO *bio;
	int i;

	if ((bio_monkey = BIO_new_packet_monkey()) == NULL)
		errx(1, "packet monkey");

	for (i = 0; i < MAX_PACKET_DELAYS; i++) {
		if (delays[i].packet == 0)
			break;
		if (!BIO_packet_monkey_delay(bio_monkey, delays[i].packet,
		    delays[i].count))
			errx(1, "delay failure");
	}

	for (i = 0; i < MAX_PACKET_DROPS; i++) {
		if (drops[i] == 0)
			break;
		if (!BIO_packet_monkey_drop(bio_monkey, drops[i]))
			errx(1, "drop failure");
	}

	if ((bio = SSL_get_wbio(ssl)) == NULL)
		errx(1, "SSL has NULL bio");

	BIO_up_ref(bio);
	bio = BIO_push(bio_monkey, bio);

	SSL_set_bio(ssl, bio, bio);
}

static int
dtlstest(const struct dtls_test *dt)
{
	SSL *client = NULL, *server = NULL;
	ssl_func *connect_func, *accept_func;
	struct sockaddr_in server_sin;
	struct pollfd pfd[2];
	int client_sock = -1;
	int server_sock = -1;
	int failed = 1;

	fprintf(stderr, "\n== Testing %s... ==\n", dt->desc);

	if (!datagram_pair(&client_sock, &server_sock, &server_sin))
		goto failure;

	if ((client = dtls_client(client_sock, &server_sin, dt->mtu)) == NULL)
		goto failure;

	if ((server = dtls_server(server_sock, dt->ssl_options, dt->mtu)) == NULL)
		goto failure;

	tls12_record_layer_set_initial_epoch(client->rl, dt->initial_epoch);
	tls12_record_layer_set_initial_epoch(server->rl, dt->initial_epoch);

	if (dt->client_bbio_off)
		SSL_set_info_callback(client, dtls_info_callback);
	if (dt->server_bbio_off)
		SSL_set_info_callback(server, dtls_info_callback);

	dtlstest_packet_monkey(client, dt->client_delays, dt->client_drops);
	dtlstest_packet_monkey(server, dt->server_delays, dt->server_drops);

	pfd[0].fd = client_sock;
	pfd[0].events = POLLOUT;
	pfd[1].fd = server_sock;
	pfd[1].events = POLLIN;

	accept_func = do_accept;
	connect_func = do_connect;

	if (dt->write_after_accept) {
		accept_func = do_accept_write;
		connect_func = do_connect_read;
	} else if (dt->shutdown_after_accept) {
		accept_func = do_accept_shutdown;
		connect_func = do_connect_shutdown;
	}

	if (!do_client_server_loop(client, connect_func, server, accept_func, pfd)) {
		fprintf(stderr, "FAIL: client and server handshake failed\n");
		goto failure;
	}

	if (dt->write_after_accept || dt->shutdown_after_accept)
		goto done;

	pfd[0].events = POLLIN;
	pfd[1].events = POLLOUT;

	if (!do_client_server_loop(client, do_read, server, do_write, pfd)) {
		fprintf(stderr, "FAIL: client read and server write I/O failed\n");
		goto failure;
	}

	pfd[0].events = POLLOUT;
	pfd[1].events = POLLIN;

	if (!do_client_server_loop(client, do_write, server, do_read, pfd)) {
		fprintf(stderr, "FAIL: client write and server read I/O failed\n");
		goto failure;
	}

	pfd[0].events = POLLOUT;
	pfd[1].events = POLLOUT;

	if (!do_client_server_loop(client, do_shutdown, server, do_shutdown, pfd)) {
		fprintf(stderr, "FAIL: client and server shutdown failed\n");
		goto failure;
	}

 done:
	fprintf(stderr, "INFO: Done!\n");

	failed = 0;

 failure:
	if (client_sock != -1)
		close(client_sock);
	if (server_sock != -1)
		close(server_sock);

	SSL_free(client);
	SSL_free(server);

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;
	size_t i;

	if (argc != 4) {
		fprintf(stderr, "usage: %s keyfile certfile cafile\n",
		    argv[0]);
		exit(1);
	}

	server_key_file = argv[1];
	server_cert_file = argv[2];
	server_ca_file = argv[3];

	for (i = 0; i < N_DTLS_TESTS; i++)
		failed |= dtlstest(&dtls_tests[i]);

	return failed;
}
