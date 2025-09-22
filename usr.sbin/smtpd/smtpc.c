/*	$OpenBSD: smtpc.c,v 1.21 2024/05/13 06:48:26 jsg Exp $	*/

/*
 * Copyright (c) 2018 Eric Faurot <eric@openbsd.org>
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

#include <event.h>
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <tls.h>
#include <unistd.h>

#include "smtp.h"
#include "log.h"

static void parse_server(char *);
static void parse_message(FILE *);
static void resume(void);

static int verbose = 1;
static int done = 0;
static int noaction = 0;
static struct addrinfo *res0, *ai;
static struct smtp_params params;
static struct smtp_mail mail;
static const char *servname = NULL;
static struct tls_config *tls_config;

static int nosni = 0;
static const char *cafile = NULL;
static const char *protocols = NULL;
static const char *ciphers = NULL;

static void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-Chnv] [-a authfile] [-F from] [-H helo] "
	    "[-s server] [-T params] [recipient ...]\n", __progname);
	exit(1);
}

static void
parse_tls_options(char *opt)
{
	static char * const tokens[] = {
#define CAFILE 0
		"cafile",
#define CIPHERS 1
		"ciphers",
#define NOSNI 2
		"nosni",
#define NOVERIFY 3
		"noverify",
#define PROTOCOLS 4
		"protocols",
#define SERVERNAME 5
		"servername",
		NULL };
	char *value;

	while (*opt) {
		switch (getsubopt(&opt, tokens, &value)) {
		case CAFILE:
			if (value == NULL)
				fatalx("missing value for cafile");
			cafile = value;
			break;
		case CIPHERS:
			if (value == NULL)
				fatalx("missing value for ciphers");
			ciphers = value;
			break;
		case NOSNI:
			if (value != NULL)
				fatalx("no value expected for nosni");
			nosni = 1;
			break;
		case NOVERIFY:
			if (value != NULL)
				fatalx("no value expected for noverify");
			params.tls_verify = 0;
			break;
		case PROTOCOLS:
			if (value == NULL)
				fatalx("missing value for protocols");
			protocols = value;
			break;
		case SERVERNAME:
			if (value == NULL)
				fatalx("missing value for servername");
			servname = value;
			break;
		case -1:
			if (suboptarg)
				fatalx("invalid TLS option \"%s\"", suboptarg);
			fatalx("missing TLS option");
		}
	}
}

int
main(int argc, char **argv)
{
	char hostname[256];
	FILE *authfile;
	int ch, i;
	uint32_t protos;
	char *server = "localhost";
	char *authstr = NULL;
	size_t alloc = 0;
	ssize_t len;
	struct passwd *pw;

	log_init(1, 0);

	if (gethostname(hostname, sizeof(hostname)) == -1)
		fatal("gethostname");

	if ((pw = getpwuid(getuid())) == NULL)
		fatal("getpwuid");

	memset(&params, 0, sizeof(params));

	params.linemax = 16392;
	params.ibufmax = 65536;
	params.obufmax = 65536;
	params.timeout = 100000;
	params.helo = hostname;

	params.tls_verify = 1;

	memset(&mail, 0, sizeof(mail));
	mail.from = pw->pw_name;

	while ((ch = getopt(argc, argv, "CF:H:S:T:a:hns:v")) != -1) {
		switch (ch) {
		case 'C':
			params.tls_verify = 0;
			break;
		case 'F':
			mail.from = optarg;
			break;
		case 'H':
			params.helo = optarg;
			break;
		case 'S':
			servname = optarg;
			break;
		case 'T':
			parse_tls_options(optarg);
			break;
		case 'a':
			if ((authfile = fopen(optarg, "r")) == NULL)
				fatal("%s: open", optarg);
			if ((len = getline(&authstr, &alloc, authfile)) == -1)
				fatal("%s: Failed to read username", optarg);
			if (authstr[len - 1] == '\n')
				authstr[len - 1] = '\0';
			params.auth_user = authstr;
			authstr = NULL;
			len = 0;
			if ((len = getline(&authstr, &alloc, authfile)) == -1)
				fatal("%s: Failed to read password", optarg);
			if (authstr[len - 1] == '\n')
				authstr[len - 1] = '\0';
			params.auth_pass = authstr;
			fclose(authfile);
			break;
		case 'h':
			usage();
			break;
		case 'n':
			noaction = 1;
			break;
		case 's':
			server = optarg;
			break;
		case 'v':
			verbose++;
			break;
		default:
			usage();
		}
	}

	log_setverbose(verbose);

	argc -= optind;
	argv += optind;

	if (argc) {
		mail.rcpt = calloc(argc, sizeof(*mail.rcpt));
		if (mail.rcpt == NULL)
			fatal("calloc");
		for (i = 0; i < argc; i++)
			mail.rcpt[i].to = argv[i];
		mail.rcptcount = argc;
	}

	event_init();

	tls_config = tls_config_new();
	if (tls_config == NULL)
		fatal("tls_config_new");

	if (protocols) {
		if (tls_config_parse_protocols(&protos, protocols) == -1)
			fatalx("failed to parse protocol '%s'", protocols);
		if (tls_config_set_protocols(tls_config, protos) == -1)
			fatalx("tls_config_set_protocols: %s",
			    tls_config_error(tls_config));
	}
	if (ciphers && tls_config_set_ciphers(tls_config, ciphers) == -1)
		fatalx("tls_config_set_ciphers: %s",
		    tls_config_error(tls_config));

	if (cafile == NULL)
		cafile = tls_default_ca_cert_file();
	if (tls_config_set_ca_file(tls_config, cafile) == -1)
		fatalx("tls_set_ca_file: %s", tls_config_error(tls_config));
	if (!params.tls_verify) {
		tls_config_insecure_noverifycert(tls_config);
		tls_config_insecure_noverifyname(tls_config);
		tls_config_insecure_noverifytime(tls_config);
	} else
		tls_config_verify(tls_config);

	if (pledge("stdio inet dns tmppath", NULL) == -1)
		fatal("pledge");

	if (!noaction)
		parse_message(stdin);

	if (pledge("stdio inet dns", NULL) == -1)
		fatal("pledge");

	parse_server(server);

	if (pledge("stdio inet", NULL) == -1)
		fatal("pledge");

	resume();

	log_debug("done...");

	return 0;
}

static void
parse_server(char *server)
{
	struct addrinfo hints;
	char *scheme, *creds, *host, *port, *p, *c;
	int error;

	creds = NULL;
	host = NULL;
	port = NULL;
	scheme = server;

	p = strstr(server, "://");
	if (p) {
		*p = '\0';
		p += 3;
		/* check for credentials */
		c = strrchr(p, '@');
		if (c) {
			creds = p;
			*c = '\0';
			host = c + 1;
		} else
			host = p;
	} else {
		/* Assume a simple server name */
		scheme = "smtp";
		host = server;
	}

	if (host[0] == '[') {
		/* IPV6 address? */
		p = strchr(host, ']');
		if (p) {
			if (p[1] == ':' || p[1] == '\0') {
				*p++ = '\0';	/* remove ']' */
				host++;		/* skip '[' */
				if (*p == ':')
					port = p + 1;
			}
		}
	}
	else {
		port = strchr(host, ':');
		if (port)
			*port++ = '\0';
	}

	if (port && port[0] == '\0')
		port = NULL;

	if (creds) {
		p = strchr(creds, ':');
		if (p == NULL)
			fatalx("invalid credentials");
		*p = '\0';

		params.auth_user = creds;
		params.auth_pass = p + 1;
	}
	params.tls_req = TLS_YES;

	if (!strcmp(scheme, "lmtp")) {
		params.lmtp = 1;
	}
	else if (!strcmp(scheme, "lmtp+tls")) {
		params.lmtp = 1;
		params.tls_req = TLS_FORCE;
	}
	else if (!strcmp(scheme, "lmtp+notls")) {
		params.lmtp = 1;
		params.tls_req = TLS_NO;
	}
	else if (!strcmp(scheme, "smtps")) {
		params.tls_req = TLS_SMTPS;
		if (port == NULL)
			port = "smtps";
	}
	else if (!strcmp(scheme, "smtp")) {
	}
	else if (!strcmp(scheme, "smtp+tls")) {
		params.tls_req = TLS_FORCE;
	}
	else if (!strcmp(scheme, "smtp+notls")) {
		params.tls_req = TLS_NO;
	}
	else
		fatalx("invalid url scheme %s", scheme);

	if (port == NULL)
		port = "smtp";

	if (servname == NULL)
		servname = host;
	if (nosni == 0)
		params.tls_servname = servname;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(host, port, &hints, &res0);
	if (error)
		fatalx("%s: %s", host, gai_strerror(error));
	ai = res0;
}

void
parse_message(FILE *ifp)
{
	char *line = NULL;
	size_t linesz = 0;
	ssize_t len;

	if ((mail.fp = tmpfile()) == NULL)
		fatal("tmpfile");

	for (;;) {
		if ((len = getline(&line, &linesz, ifp)) == -1) {
			if (feof(ifp))
				break;
			fatal("getline");
		}

		if (len >= 2 && line[len - 2] == '\r' && line[len - 1] == '\n')
			line[--len - 1] = '\n';

		if (fwrite(line, 1, len, mail.fp) != len)
			fatal("fwrite");

		if (line[len - 1] != '\n' && fputc('\n', mail.fp) == EOF)
			fatal("fputc");
	}

	free(line);
	fclose(ifp);
	rewind(mail.fp);
}

void
resume(void)
{
	static int started = 0;
	char host[256];
	char serv[16];

	if (done) {
		event_loopexit(NULL);
		return;
	}

	if (ai == NULL)
		fatalx("no more host");

	getnameinfo(ai->ai_addr, ai->ai_addr->sa_len,
	    host, sizeof(host), serv, sizeof(serv),
	    NI_NUMERICHOST | NI_NUMERICSERV);
	log_debug("trying host %s port %s...", host, serv);

	params.dst = ai->ai_addr;
	if (smtp_connect(&params, NULL) == NULL)
		fatal("smtp_connect");

	if (started == 0) {
		started = 1;
		event_loop(0);
	}
}

void
log_trace(int lvl, const char *emsg, ...)
{
	va_list ap;

	if (verbose > lvl) {
		va_start(ap, emsg);
		vlog(LOG_DEBUG, emsg, ap);
		va_end(ap);
	}
}

void
smtp_require_tls(void *tag, struct smtp_client *proto)
{
	struct tls *tls;

	tls = tls_client();
	if (tls == NULL)
		fatal("tls_client");

	if (tls_configure(tls, tls_config) == -1)
		fatalx("tls_configure: %s", tls_error(tls));

	smtp_set_tls(proto, tls);
}

void
smtp_ready(void *tag, struct smtp_client *proto)
{
	log_debug("connection ready...");

	if (done || noaction)
		smtp_quit(proto);
	else
		smtp_sendmail(proto, &mail);
}

void
smtp_failed(void *tag, struct smtp_client *proto, int failure, const char *detail)
{
	switch (failure) {
	case FAIL_INTERNAL:
		log_warnx("internal error: %s", detail);
		break;
	case FAIL_CONN:
		log_warnx("connection error: %s", detail);
		break;
	case FAIL_PROTO:
		log_warnx("protocol error: %s", detail);
		break;
	case FAIL_IMPL:
		log_warnx("missing feature: %s", detail);
		break;
	case FAIL_RESP:
		log_warnx("rejected by server: %s", detail);
		break;
	default:
		fatalx("unknown failure %d: %s", failure, detail);
	}
}

void
smtp_status(void *tag, struct smtp_client *proto, struct smtp_status *status)
{
	log_info("%s: %s: %s", status->rcpt->to, status->cmd, status->status);
}

void
smtp_done(void *tag, struct smtp_client *proto, struct smtp_mail *mail)
{
	int i;

	log_debug("mail done...");

	if (noaction)
		return;

	for (i = 0; i < mail->rcptcount; i++)
		if (!mail->rcpt[i].done)
			return;

	done = 1;
}

void
smtp_closed(void *tag, struct smtp_client *proto)
{
	log_debug("connection closed...");

	ai = ai->ai_next;
	if (noaction && ai == NULL)
		done = 1;

	resume();
}
