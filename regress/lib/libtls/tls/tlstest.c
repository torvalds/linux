/* $OpenBSD: tlstest.c,v 1.17 2025/06/04 10:28:00 tb Exp $ */
/*
 * Copyright (c) 2017 Joel Sing <jsing@openbsd.org>
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

#include <sys/socket.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <tls.h>

#define CIRCULAR_BUFFER_SIZE 512

unsigned char client_buffer[CIRCULAR_BUFFER_SIZE];
unsigned char *client_readptr, *client_writeptr;

unsigned char server_buffer[CIRCULAR_BUFFER_SIZE];
unsigned char *server_readptr, *server_writeptr;

char *cafile, *certfile, *keyfile;

int debug = 0;

static void
circular_init(void)
{
	client_readptr = client_writeptr = client_buffer;
	server_readptr = server_writeptr = server_buffer;
}

static ssize_t
circular_read(char *name, unsigned char *buf, size_t bufsize,
    unsigned char **readptr, unsigned char *writeptr,
    unsigned char *outbuf, size_t outlen)
{
	unsigned char *nextptr = *readptr;
	size_t n = 0;

	while (n < outlen) {
		if (nextptr == writeptr)
			break;
		*outbuf++ = *nextptr++;
		if ((size_t)(nextptr - buf) >= bufsize)
			nextptr = buf;
		*readptr = nextptr;
		n++;
	}

	if (debug && n > 0)
		fprintf(stderr, "%s buffer: read %zi bytes\n", name, n);

	return (n > 0 ? (ssize_t)n : TLS_WANT_POLLIN);
}

static ssize_t
circular_write(char *name, unsigned char *buf, size_t bufsize,
    unsigned char *readptr, unsigned char **writeptr,
    const unsigned char *inbuf, size_t inlen)
{
	unsigned char *nextptr = *writeptr;
	unsigned char *prevptr;
	size_t n = 0;

	while (n < inlen) {
		prevptr = nextptr++;
		if ((size_t)(nextptr - buf) >= bufsize)
			nextptr = buf;
		if (nextptr == readptr)
			break;
		*prevptr = *inbuf++;
		*writeptr = nextptr;
		n++;
	}

	if (debug && n > 0)
		fprintf(stderr, "%s buffer: wrote %zi bytes\n", name, n);

	return (n > 0 ? (ssize_t)n : TLS_WANT_POLLOUT);
}

static ssize_t
client_read(struct tls *ctx, void *buf, size_t buflen, void *cb_arg)
{
	return circular_read("client", client_buffer, sizeof(client_buffer),
	    &client_readptr, client_writeptr, buf, buflen);
}

static ssize_t
client_write(struct tls *ctx, const void *buf, size_t buflen, void *cb_arg)
{
	return circular_write("server", server_buffer, sizeof(server_buffer),
	    server_readptr, &server_writeptr, buf, buflen);
}

static ssize_t
server_read(struct tls *ctx, void *buf, size_t buflen, void *cb_arg)
{
	return circular_read("server", server_buffer, sizeof(server_buffer),
	    &server_readptr, server_writeptr, buf, buflen);
}

static ssize_t
server_write(struct tls *ctx, const void *buf, size_t buflen, void *cb_arg)
{
	return circular_write("client", client_buffer, sizeof(client_buffer),
	    client_readptr, &client_writeptr, buf, buflen);
}

static int
do_tls_handshake(char *name, struct tls *ctx)
{
	int rv;

	rv = tls_handshake(ctx);
	if (rv == 0)
		return (1);
	if (rv == TLS_WANT_POLLIN || rv == TLS_WANT_POLLOUT)
		return (0);

	errx(1, "%s handshake failed: %s", name, tls_error(ctx));
}

static int
do_tls_close(char *name, struct tls *ctx)
{
	int rv;

	rv = tls_close(ctx);
	if (rv == 0)
		return (1);
	if (rv == TLS_WANT_POLLIN || rv == TLS_WANT_POLLOUT)
		return (0);

	errx(1, "%s close failed: %s", name, tls_error(ctx));
}

static int
do_client_server_handshake(char *desc, struct tls *client,
    struct tls *server_cctx)
{
	int i, client_done, server_done;

	i = client_done = server_done = 0;
	do {
		if (client_done == 0)
			client_done = do_tls_handshake("client", client);
		if (server_done == 0)
			server_done = do_tls_handshake("server", server_cctx);
	} while (i++ < 100 && (client_done == 0 || server_done == 0));

	if (client_done == 0 || server_done == 0) {
		printf("FAIL: %s TLS handshake did not complete\n", desc);
		return (1);
	}

	return (0);
}

static int
do_client_server_close(char *desc, struct tls *client, struct tls *server_cctx)
{
	int i, client_done, server_done;

	i = client_done = server_done = 0;
	do {
		if (client_done == 0)
			client_done = do_tls_close("client", client);
		if (server_done == 0)
			server_done = do_tls_close("server", server_cctx);
	} while (i++ < 100 && (client_done == 0 || server_done == 0));

	if (client_done == 0 || server_done == 0) {
		printf("FAIL: %s TLS close did not complete\n", desc);
		return (1);
	}

	return (0);
}

static int
do_client_server_test(char *desc, struct tls *client, struct tls *server_cctx)
{
	if (do_client_server_handshake(desc, client, server_cctx) != 0)
		return (1);

	printf("INFO: %s TLS handshake completed successfully\n", desc);

	/* XXX - Do some reads and writes... */

	if (do_client_server_close(desc, client, server_cctx) != 0)
		return (1);

	printf("INFO: %s TLS close completed successfully\n", desc);

	return (0);
}

static int
test_tls_cbs(struct tls *client, struct tls *server)
{
	struct tls *server_cctx;
	int failure;

	circular_init();

	if (tls_accept_cbs(server, &server_cctx, server_read, server_write,
	    NULL) == -1)
		errx(1, "failed to accept: %s", tls_error(server));

	if (tls_connect_cbs(client, client_read, client_write, NULL,
	    "test") == -1)
		errx(1, "failed to connect: %s", tls_error(client));

	failure = do_client_server_test("callback", client, server_cctx);

	tls_free(server_cctx);

	return (failure);
}

static int
test_tls_fds(struct tls *client, struct tls *server)
{
	struct tls *server_cctx;
	int cfds[2], sfds[2];
	int failure;

	if (pipe2(cfds, O_NONBLOCK) == -1)
		err(1, "failed to create pipe");
	if (pipe2(sfds, O_NONBLOCK) == -1)
		err(1, "failed to create pipe");

	if (tls_accept_fds(server, &server_cctx, sfds[0], cfds[1]) == -1)
		errx(1, "failed to accept: %s", tls_error(server));

	if (tls_connect_fds(client, cfds[0], sfds[1], "test") == -1)
		errx(1, "failed to connect: %s", tls_error(client));

	failure = do_client_server_test("file descriptor", client, server_cctx);

	tls_free(server_cctx);

	close(cfds[0]);
	close(cfds[1]);
	close(sfds[0]);
	close(sfds[1]);

	return (failure);
}

static int
test_tls_socket(struct tls *client, struct tls *server)
{
	struct tls *server_cctx;
	int failure;
	int sv[2];

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, PF_UNSPEC,
	    sv) == -1)
		err(1, "failed to create socketpair");

	if (tls_accept_socket(server, &server_cctx, sv[0]) == -1)
		errx(1, "failed to accept: %s", tls_error(server));

	if (tls_connect_socket(client, sv[1], "test") == -1)
		errx(1, "failed to connect: %s", tls_error(client));

	failure = do_client_server_test("socket", client, server_cctx);

	tls_free(server_cctx);

	close(sv[0]);
	close(sv[1]);

	return (failure);
}

static int
test_tls(char *client_protocols, char *server_protocols, char *ciphers)
{
	struct tls_config *client_cfg, *server_cfg;
	struct tls *client, *server;
	uint32_t protocols;
	int failure = 0;

	if ((client = tls_client()) == NULL)
		errx(1, "failed to create tls client");
	if ((client_cfg = tls_config_new()) == NULL)
		errx(1, "failed to create tls client config");
	tls_config_insecure_noverifyname(client_cfg);
	if (tls_config_parse_protocols(&protocols, client_protocols) == -1)
		errx(1, "failed to parse protocols: %s", tls_config_error(client_cfg));
	if (tls_config_set_protocols(client_cfg, protocols) == -1)
		errx(1, "failed to set protocols: %s", tls_config_error(client_cfg));
	if (tls_config_set_ciphers(client_cfg, ciphers) == -1)
		errx(1, "failed to set ciphers: %s", tls_config_error(client_cfg));
	if (tls_config_set_ca_file(client_cfg, cafile) == -1)
		errx(1, "failed to set ca: %s", tls_config_error(client_cfg));

	if ((server = tls_server()) == NULL)
		errx(1, "failed to create tls server");
	if ((server_cfg = tls_config_new()) == NULL)
		errx(1, "failed to create tls server config");
	if (tls_config_parse_protocols(&protocols, server_protocols) == -1)
		errx(1, "failed to parse protocols: %s", tls_config_error(server_cfg));
	if (tls_config_set_protocols(server_cfg, protocols) == -1)
		errx(1, "failed to set protocols: %s", tls_config_error(server_cfg));
	if (tls_config_set_ciphers(server_cfg, ciphers) == -1)
		errx(1, "failed to set ciphers: %s", tls_config_error(server_cfg));
	if (tls_config_set_keypair_file(server_cfg, certfile, keyfile) == -1)
		errx(1, "failed to set keypair: %s",
		    tls_config_error(server_cfg));

	if (tls_configure(client, client_cfg) == -1)
		errx(1, "failed to configure client: %s", tls_error(client));
	tls_reset(server);
	if (tls_configure(server, server_cfg) == -1)
		errx(1, "failed to configure server: %s", tls_error(server));

	tls_config_free(client_cfg);
	tls_config_free(server_cfg);

	failure |= test_tls_cbs(client, server);

	tls_free(client);
	tls_free(server);

	return (failure);
}

static int
do_tls_tests(void)
{
	struct tls_config *client_cfg, *server_cfg;
	struct tls *client, *server;
	int failure = 0;

	printf("== TLS tests ==\n");

	if ((client = tls_client()) == NULL)
		errx(1, "failed to create tls client");
	if ((client_cfg = tls_config_new()) == NULL)
		errx(1, "failed to create tls client config");
	tls_config_insecure_noverifyname(client_cfg);
	if (tls_config_set_ca_file(client_cfg, cafile) == -1)
		errx(1, "failed to set ca: %s", tls_config_error(client_cfg));

	if ((server = tls_server()) == NULL)
		errx(1, "failed to create tls server");
	if ((server_cfg = tls_config_new()) == NULL)
		errx(1, "failed to create tls server config");
	if (tls_config_set_keypair_file(server_cfg, certfile, keyfile) == -1)
		errx(1, "failed to set keypair: %s",
		    tls_config_error(server_cfg));

	tls_reset(client);
	if (tls_configure(client, client_cfg) == -1)
		errx(1, "failed to configure client: %s", tls_error(client));
	tls_reset(server);
	if (tls_configure(server, server_cfg) == -1)
		errx(1, "failed to configure server: %s", tls_error(server));

	failure |= test_tls_cbs(client, server);

	tls_reset(client);
	if (tls_configure(client, client_cfg) == -1)
		errx(1, "failed to configure client: %s", tls_error(client));
	tls_reset(server);
	if (tls_configure(server, server_cfg) == -1)
		errx(1, "failed to configure server: %s", tls_error(server));

	failure |= test_tls_fds(client, server);

	tls_reset(client);
	if (tls_configure(client, client_cfg) == -1)
		errx(1, "failed to configure client: %s", tls_error(client));
	tls_reset(server);
	if (tls_configure(server, server_cfg) == -1)
		errx(1, "failed to configure server: %s", tls_error(server));

	tls_config_free(client_cfg);
	tls_config_free(server_cfg);

	failure |= test_tls_socket(client, server);

	tls_free(client);
	tls_free(server);

	printf("\n");

	return (failure);
}

static int
do_tls_ordering_tests(void)
{
	struct tls *client = NULL, *server = NULL, *server_cctx = NULL;
	struct tls_config *client_cfg, *server_cfg;
	int failure = 0;

	printf("== TLS ordering tests ==\n");

	if ((client = tls_client()) == NULL)
		errx(1, "failed to create tls client");
	if ((client_cfg = tls_config_new()) == NULL)
		errx(1, "failed to create tls client config");
	tls_config_insecure_noverifyname(client_cfg);
	if (tls_config_set_ca_file(client_cfg, cafile) == -1)
		errx(1, "failed to set ca: %s", tls_config_error(client_cfg));

	if ((server = tls_server()) == NULL)
		errx(1, "failed to create tls server");
	if ((server_cfg = tls_config_new()) == NULL)
		errx(1, "failed to create tls server config");
	if (tls_config_set_keypair_file(server_cfg, certfile, keyfile) == -1)
		errx(1, "failed to set keypair: %s",
		    tls_config_error(server_cfg));

	if (tls_configure(client, client_cfg) == -1)
		errx(1, "failed to configure client: %s", tls_error(client));
	if (tls_configure(server, server_cfg) == -1)
		errx(1, "failed to configure server: %s", tls_error(server));

	tls_config_free(client_cfg);
	tls_config_free(server_cfg);

	if (tls_handshake(client) != -1) {
		printf("FAIL: TLS handshake succeeded on unconnnected "
		    "client context\n");
		failure = 1;
		goto done;
	}

	circular_init();

	if (tls_accept_cbs(server, &server_cctx, server_read, server_write,
	    NULL) == -1)
		errx(1, "failed to accept: %s", tls_error(server));

	if (tls_connect_cbs(client, client_read, client_write, NULL,
	    "test") == -1)
		errx(1, "failed to connect: %s", tls_error(client));

	if (do_client_server_handshake("ordering", client, server_cctx) != 0) {
		failure = 1;
		goto done;
	}

	if (tls_handshake(client) != -1) {
		printf("FAIL: TLS handshake succeeded twice\n");
		failure = 1;
		goto done;
	}

	if (tls_handshake(server_cctx) != -1) {
		printf("FAIL: TLS handshake succeeded twice\n");
		failure = 1;
		goto done;
	}

	if (do_client_server_close("ordering", client, server_cctx) != 0) {
		failure = 1;
		goto done;
	}

 done:
	tls_free(client);
	tls_free(server);
	tls_free(server_cctx);

	printf("\n");

	return (failure);
}

struct test_versions {
	char *client;
	char *server;
};

static struct test_versions tls_test_versions[] = {
	{"tlsv1.3", "all"},
	{"tlsv1.2", "all"},
	{"all", "tlsv1.3"},
	{"all", "tlsv1.2"},
	{"all:!tlsv1.1", "tlsv1.2"},
	{"all:!tlsv1.2", "tlsv1.3"},
	{"all:!tlsv1.3", "tlsv1.2"},
	{"all:!tlsv1.2:!tlsv1.1", "tlsv1.3"},
	{"all:!tlsv1.2:!tlsv1.1:!tlsv1.0", "tlsv1.3"},
	{"tlsv1.3", "tlsv1.3"},
	{"tlsv1.2", "tlsv1.2"},
};

#define N_TLS_VERSION_TESTS \
    (sizeof(tls_test_versions) / sizeof(*tls_test_versions))

static int
do_tls_version_tests(void)
{
	struct test_versions *tv;
	int failure = 0;
	size_t i;

	printf("== TLS version tests ==\n");

	for (i = 0; i < N_TLS_VERSION_TESTS; i++) {
		tv = &tls_test_versions[i];
		printf("INFO: version test %zu - client versions '%s' "
		    "and server versions '%s'\n", i, tv->client, tv->server);
		failure |= test_tls(tv->client, tv->server, "legacy");
		printf("\n");
	}

	return failure;
}

static int
test_tls_alpn(const char *client_alpn, const char *server_alpn,
    const char *selected)
{
	struct tls_config *client_cfg, *server_cfg;
	struct tls *client, *server, *server_cctx;
	const char *got_server, *got_client;
	int failed = 1;

	if ((client = tls_client()) == NULL)
		errx(1, "failed to create tls client");
	if ((client_cfg = tls_config_new()) == NULL)
		errx(1, "failed to create tls client config");
	tls_config_insecure_noverifyname(client_cfg);
	if (tls_config_set_alpn(client_cfg, client_alpn) == -1)
		errx(1, "failed to set alpn: %s", tls_config_error(client_cfg));
	if (tls_config_set_ca_file(client_cfg, cafile) == -1)
		errx(1, "failed to set ca: %s", tls_config_error(client_cfg));

	if ((server = tls_server()) == NULL)
		errx(1, "failed to create tls server");
	if ((server_cfg = tls_config_new()) == NULL)
		errx(1, "failed to create tls server config");
	if (tls_config_set_alpn(server_cfg, server_alpn) == -1)
		errx(1, "failed to set alpn: %s", tls_config_error(server_cfg));
	if (tls_config_set_keypair_file(server_cfg, certfile, keyfile) == -1)
		errx(1, "failed to set keypair: %s",
		    tls_config_error(server_cfg));

	if (tls_configure(client, client_cfg) == -1)
		errx(1, "failed to configure client: %s", tls_error(client));
	tls_reset(server);
	if (tls_configure(server, server_cfg) == -1)
		errx(1, "failed to configure server: %s", tls_error(server));

	tls_config_free(client_cfg);
	tls_config_free(server_cfg);

	circular_init();

	if (tls_accept_cbs(server, &server_cctx, server_read, server_write,
	    NULL) == -1)
		errx(1, "failed to accept: %s", tls_error(server));

	if (tls_connect_cbs(client, client_read, client_write, NULL,
	    "test") == -1)
		errx(1, "failed to connect: %s", tls_error(client));

	if (do_client_server_test("alpn", client, server_cctx) != 0)
		goto fail;

	got_server = tls_conn_alpn_selected(server_cctx);
	got_client = tls_conn_alpn_selected(client);

	if (got_server == NULL || got_client == NULL) {
		printf("FAIL: expected ALPN for server and client, got "
		    "server: %p, client %p\n", got_server, got_client);
		goto fail;
	}

	if (strcmp(got_server, got_client) != 0) {
		printf("FAIL: ALPN mismatch: server %s, client %s\n",
		    got_server, got_client);
		goto fail;
	}

	if (strcmp(selected, got_server) != 0) {
		printf("FAIL: ALPN mismatch: want %s, got %s\n",
		    selected, got_server);
		goto fail;
	}

	failed = 0;

 fail:
	tls_free(client);
	tls_free(server);
	tls_free(server_cctx);

	return (failed);
}

static const struct test_alpn {
	const char *client;
	const char *server;
	const char *selected;
} tls_test_alpn[] = {
	{
		.client = "http/2,http/1.1",
		.server = "http/1.1",
		.selected = "http/1.1",
	},
	{
		.client = "http/2,http/1.1",
		.server = "http/2,http/1.1",
		.selected = "http/2",
	},
	{
		.client = "http/1.1,http/2",
		.server = "http/2,http/1.1",
		.selected = "http/2",
	},
	{
		.client = "http/2,http/1.1",
		.server = "http/1.1,http/2",
		.selected = "http/1.1",
	},
	{
		.client = "http/1.1",
		.server = "http/2,http/1.1",
		.selected = "http/1.1",
	},
};

#define N_TLS_ALPN_TESTS (sizeof(tls_test_alpn) / sizeof(tls_test_alpn[0]))

static int
do_tls_alpn_tests(void)
{
	const struct test_alpn *ta;
	int failure = 0;
	size_t i;

	printf("== TLS alpn tests ==\n");

	for (i = 0; i < N_TLS_ALPN_TESTS; i++) {
		ta = &tls_test_alpn[i];
		printf("INFO: alpn test %zu - client alpn '%s' "
		    "and server alpn '%s'\n", i, ta->client, ta->server);
		failure |= test_tls_alpn(ta->client, ta->server, ta->selected);
		printf("\n");
	}

	return failure;
}

int
main(int argc, char **argv)
{
	int failure = 0;

	if (argc != 4) {
		fprintf(stderr, "usage: %s cafile certfile keyfile\n",
		    argv[0]);
		return (1);
	}

	cafile = argv[1];
	certfile = argv[2];
	keyfile = argv[3];

	failure |= do_tls_tests();
	failure |= do_tls_ordering_tests();
	failure |= do_tls_version_tests();
	failure |= do_tls_alpn_tests();

	return (failure);
}
