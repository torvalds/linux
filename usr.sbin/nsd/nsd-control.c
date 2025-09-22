/*
 * nsd-control.c - remote control utility for nsd.
 *
 * Copyright (c) 2011, NLnet Labs. All rights reserved.
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
 * The remote control utility contacts the nsd server over ssl and
 * sends the command, receives the answer, and displays the result
 * from the commandline.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>

struct region;
struct domain_table;
struct zone;
struct domain;
int zonec_parse_string(struct region* ATTR_UNUSED(region),
	struct domain_table* ATTR_UNUSED(domains),
	struct zone* ATTR_UNUSED(zone), char* ATTR_UNUSED(str),
	struct domain** ATTR_UNUSED(parsed), int* ATTR_UNUSED(num_rrs))
{
	return 0;
}

#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#ifdef HAVE_SSL
#ifdef HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif
#ifdef HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif
#ifdef HAVE_OPENSSL_RAND_H
#include <openssl/rand.h>
#endif
#endif /* HAVE_SSL */
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#include <fcntl.h>
#ifndef AF_LOCAL
#define AF_LOCAL AF_UNIX
#endif
#include "util.h"
#include "tsig.h"
#include "options.h"
#include "zonec.h"

static void usage(void) ATTR_NORETURN;
#ifdef HAVE_SSL
static void ssl_err(const char* s) ATTR_NORETURN;
static void ssl_path_err(const char* s, const char *path) ATTR_NORETURN;
#else
/* define SSL to use as a boolean to turn it off in function calls. */
#define SSL int
#endif

/** timeout to wait for connection over stream, in msec */
#define NSD_CONTROL_CONNECT_TIMEOUT 5000

/** Give nsd-control usage, and exit (1). */
static void
usage()
{
	printf("Usage:	nsd-control [options] command\n");
	printf("	Remote control utility for nsd server.\n");
	printf("Version %s. Report bugs to <%s>.\n",
		PACKAGE_VERSION, PACKAGE_BUGREPORT);
	printf("Options:\n");
	printf("  -c file	config file, default is %s\n", CONFIGFILE);
	printf("  -s ip[@port]	server address, if omitted config is used.\n");
	printf("  -h		show this usage help.\n");
	printf("Commands:\n");
	printf("  start				start server; runs nsd(8)\n");
	printf("  stop				stops the server\n");
	printf("  reload [<zone>]		reload modified zonefiles from disk\n");
	printf("  reconfig			reload the config file\n");
	printf("  repattern			the same as reconfig\n");
	printf("  log_reopen			reopen logfile (for log rotate)\n");
	printf("  status			display status of server\n");
	printf("  stats				print statistics\n");
	printf("  stats_noreset			peek at statistics\n");
	printf("  addzone <name> <pattern>	add a new zone\n");
	printf("  delzone <name>		remove a zone\n");
	printf("  changezone <name> <pattern>	change zone to use pattern\n");
	printf("  addzones			add zone list on stdin {name space pattern newline}\n");
	printf("  delzones			remove zone list on stdin {name newline}\n");
	printf("  write [<zone>]		write changed zonefiles to disk\n");
	printf("  notify [<zone>]		send NOTIFY messages to secondary servers\n");
	printf("  transfer [<zone>]		try to update secondary zones to newer serial\n");
	printf("  force_transfer [<zone>]	update secondary zones with AXFR, no serial check\n");
	printf("  zonestatus [<zone>]		print state, serial, activity\n");
	printf("  serverpid			get pid of server process\n");
	printf("  verbosity <number>		change logging detail\n");
	printf("  print_tsig [<key_name>]	print tsig with <name> the secret and algo\n");
	printf("  update_tsig <name> <secret>	change existing tsig with <name> to a new <secret>\n");
	printf("  add_tsig <name> <secret> [algo] add new key with the given parameters\n");
	printf("  assoc_tsig <zone> <key_name>	associate <zone> with given tsig <key_name> name\n");
	printf("  del_tsig <key_name>		delete tsig <key_name> from configuration\n");
	printf("  add_cookie_secret <secret>	add (or replace) a new cookie secret <secret>\n");
	printf("  drop_cookie_secret		drop a staging cookie secret\n");
	printf("  activate_cookie_secret	make a staging cookie secret active\n");
	printf("  print_cookie_secrets		show all cookie secrets with their status\n");
	exit(1);
}

#ifdef HAVE_SSL
/** exit with ssl error */
static void ssl_err(const char* s)
{
	fprintf(stderr, "error: %s\n", s);
	ERR_print_errors_fp(stderr);
	exit(1);
}

/** exit with ssl error related to a file path */
static void ssl_path_err(const char* s, const char *path)
{
	unsigned long err;
	err = ERR_peek_error();
	if (ERR_GET_LIB(err) == ERR_LIB_SYS) {
		fprintf(stderr, "error: %s\n%s: %s\n",
			s, path, ERR_reason_error_string(err));
		exit(1);
	} else {
		ssl_err(s);
	}
}

/** setup SSL context */
static SSL_CTX*
setup_ctx(struct nsd_options* cfg)
{
	char* s_cert, *c_key, *c_cert;
	SSL_CTX* ctx;

	if(!options_remote_is_address(cfg))
		return NULL;
	s_cert = cfg->server_cert_file;
	c_key = cfg->control_key_file;
	c_cert = cfg->control_cert_file;

	/* filenames may be relative to zonesdir */
	if (cfg->zonesdir && cfg->zonesdir[0] &&
		(s_cert[0] != '/' || c_key[0] != '/' || c_cert[0] != '/')) {
		if(chdir(cfg->zonesdir))
			error("could not chdir to zonesdir: %s %s",
				cfg->zonesdir, strerror(errno));
	}

        ctx = SSL_CTX_new(SSLv23_client_method());
	if(!ctx)
		ssl_err("could not allocate SSL_CTX pointer");
#if SSL_OP_NO_SSLv2 != 0
        if((SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2) & SSL_OP_NO_SSLv2)
		!= SSL_OP_NO_SSLv2)
		ssl_err("could not set SSL_OP_NO_SSLv2");
#endif
        if((SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv3) & SSL_OP_NO_SSLv3)
		!= SSL_OP_NO_SSLv3)
		ssl_err("could not set SSL_OP_NO_SSLv3");
#if defined(SSL_OP_NO_RENEGOTIATION)
	/* disable client renegotiation */
	if((SSL_CTX_set_options(ctx, SSL_OP_NO_RENEGOTIATION) &
		SSL_OP_NO_RENEGOTIATION) != SSL_OP_NO_RENEGOTIATION)
		ssl_err("could not set SSL_OP_NO_RENEGOTIATION");
#endif
	if(!SSL_CTX_use_certificate_file(ctx,c_cert,SSL_FILETYPE_PEM))
		ssl_path_err("Error setting up SSL_CTX client cert", c_cert);
	if(!SSL_CTX_use_PrivateKey_file(ctx,c_key,SSL_FILETYPE_PEM))
		ssl_path_err("Error setting up SSL_CTX client key", c_key);
	if(!SSL_CTX_check_private_key(ctx))
		ssl_err("Error setting up SSL_CTX client key");
	if (SSL_CTX_load_verify_locations(ctx, s_cert, NULL) != 1)
		ssl_path_err("Error setting up SSL_CTX verify, server cert",
			s_cert);
	SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

	return ctx;
}
#endif /* HAVE_SSL */

/** check connect error */
static void
checkconnecterr(int err, const char* svr, int port, int statuscmd)
{
	if(!port) fprintf(stderr, "error: connect (%s): %s\n", svr,
		strerror(err));
	else fprintf(stderr, "error: connect (%s@%d): %s\n", svr, port,
		strerror(err));
	if(err == ECONNREFUSED && statuscmd) {
		printf("nsd is stopped\n");
		exit(3);
	}
	exit(1);
}

/** contact the server with TCP connect */
static int
contact_server(const char* svr, struct nsd_options* cfg, int statuscmd)
{
#ifdef INET6
	struct sockaddr_storage addr;
#else
	struct sockaddr_in addr;
#endif
	socklen_t addrlen;
	int fd;
	int port = cfg->control_port;
	int addrfamily = 0;
	/* use svr or a config entry */
	if(!svr) {
		if(cfg->control_interface) {
			svr = cfg->control_interface->address;
		} else if(cfg->do_ip4) {
			svr = "127.0.0.1";
		} else {
			svr = "::1";
		}
		/* config 0 addr (everything), means ask localhost */
		if(strcmp(svr, "0.0.0.0") == 0)
			svr = "127.0.0.1";
		else if(strcmp(svr, "::0") == 0 ||
			strcmp(svr, "0::0") == 0 ||
			strcmp(svr, "0::") == 0 ||
			strcmp(svr, "::") == 0)
			svr = "::1";
	}
	if(strchr(svr, '@')) {
		char* ps = strchr(svr, '@');
		*ps++ = 0;
		port = atoi(ps);
		if(!port) {
			fprintf(stderr, "could not parse port %s\n", ps);
			exit(1);
		}
	} 
	if(svr[0] == '/') {
#ifdef HAVE_SYS_UN_H
		struct sockaddr_un* usock = (struct sockaddr_un *) &addr;
		usock->sun_family = AF_LOCAL;
#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
		usock->sun_len = (unsigned)sizeof(usock);
#endif
		(void)strlcpy(usock->sun_path, svr, sizeof(usock->sun_path));
		addrlen = (socklen_t)sizeof(struct sockaddr_un);
		addrfamily = AF_LOCAL;
		port = 0;
#endif
#ifdef INET6
	} else if(strchr(svr, ':')) {
		struct sockaddr_in6 sa;
		addrlen = (socklen_t)sizeof(struct sockaddr_in6);
		memset(&sa, 0, addrlen);
		sa.sin6_family = AF_INET6;
		sa.sin6_port = (in_port_t)htons((uint16_t)port);
		if(inet_pton((int)sa.sin6_family, svr, &sa.sin6_addr) <= 0) {
			fprintf(stderr, "could not parse IP: %s\n", svr);
			exit(1);
		}
		memcpy(&addr, &sa, addrlen);
		addrfamily = AF_INET6;
#endif
	} else { /* ip4 */
		struct sockaddr_in sa;
		addrlen = (socklen_t)sizeof(struct sockaddr_in);
		memset(&sa, 0, addrlen);
		sa.sin_family = AF_INET;
		sa.sin_port = (in_port_t)htons((uint16_t)port);
		if(inet_pton((int)sa.sin_family, svr, &sa.sin_addr) <= 0) {
			fprintf(stderr, "could not parse IP: %s\n", svr);
			exit(1);
		}
		memcpy(&addr, &sa, addrlen);
		addrfamily = AF_INET;
	}

	fd = socket(addrfamily, SOCK_STREAM, 0);
	if(fd == -1) {
		fprintf(stderr, "socket: %s\n", strerror(errno));
		exit(1);
	}
	if(fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
		fprintf(stderr, "error: set nonblocking: fcntl: %s",
			strerror(errno));
	}
	if(connect(fd, (struct sockaddr*)&addr, addrlen) < 0) {
		if(errno != EINPROGRESS) {
			checkconnecterr(errno, svr, port, statuscmd);
		}
	}
	while(1) {
		fd_set rset, wset, eset;
		struct timeval tv;
		FD_ZERO(&rset);
		FD_SET(fd, &rset);
		FD_ZERO(&wset);
		FD_SET(fd, &wset);
		FD_ZERO(&eset);
		FD_SET(fd, &eset);
		tv.tv_sec = NSD_CONTROL_CONNECT_TIMEOUT/1000;
		tv.tv_usec= (NSD_CONTROL_CONNECT_TIMEOUT%1000)*1000;
		if(select(fd+1, &rset, &wset, &eset, &tv) == -1) {
			fprintf(stderr, "select: %s\n", strerror(errno));
			exit(1);
		}
		if(!FD_ISSET(fd, &rset) && !FD_ISSET(fd, &wset) &&
			!FD_ISSET(fd, &eset)) {
			fprintf(stderr, "timeout: could not connect to server\n");
			exit(1);
		} else {
			/* check nonblocking connect error */
			int error = 0;
			socklen_t len = (socklen_t)sizeof(error);
			if(getsockopt(fd, SOL_SOCKET, SO_ERROR, (void*)&error,
				&len) < 0) {
				error = errno; /* on solaris errno is error */
			}
			if(error != 0) {
				if(error == EINPROGRESS || error == EWOULDBLOCK)
					continue; /* try again later */
				checkconnecterr(error, svr, port, statuscmd);
			}
		}
		break;
	}
	if(fcntl(fd, F_SETFL, 0) == -1) {
		fprintf(stderr, "error: set blocking: fcntl: %s",
			strerror(errno));
	}
	return fd;
}

#ifdef HAVE_SSL
/** setup SSL on the connection */
static SSL*
setup_ssl(SSL_CTX* ctx, int fd)
{
	SSL* ssl;
	X509* x;
	int r;

	if(!ctx) return NULL;
	ssl = SSL_new(ctx);
	if(!ssl)
		ssl_err("could not SSL_new");
	SSL_set_connect_state(ssl);
	(void)SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
	if(!SSL_set_fd(ssl, fd))
		ssl_err("could not SSL_set_fd");
	while(1) {
		ERR_clear_error();
		if( (r=SSL_do_handshake(ssl)) == 1)
			break;
		r = SSL_get_error(ssl, r);
		if(r != SSL_ERROR_WANT_READ && r != SSL_ERROR_WANT_WRITE)
			ssl_err("SSL handshake failed");
		/* wants to be called again */
	}

	/* check authenticity of server */
	if(SSL_get_verify_result(ssl) != X509_V_OK)
		ssl_err("SSL verification failed");
#ifdef HAVE_SSL_GET1_PEER_CERTIFICATE
	x = SSL_get1_peer_certificate(ssl);
#else
	x = SSL_get_peer_certificate(ssl);
#endif
	if(!x)
		ssl_err("Server presented no peer certificate");
	X509_free(x);
	return ssl;
}
#endif /* HAVE_SSL */

/** read from ssl or fd, fatalexit on error, 0 EOF, 1 success */
static int
remote_read(SSL* ssl, int fd, char* buf, size_t len)
{
	if(ssl) {
#ifdef HAVE_SSL
		int r;
		ERR_clear_error();
		if((r = SSL_read(ssl, buf, (int)len-1)) <= 0) {
			if(SSL_get_error(ssl, r) == SSL_ERROR_ZERO_RETURN) {
				/* EOF */
				return 0;
			}
			ssl_err("could not SSL_read");
		}
		buf[r] = 0;
#endif /* HAVE_SSL */
	} else {
		ssize_t rr = read(fd, buf, len-1);
		if(rr <= 0) {
			if(rr == 0) {
				/* EOF */
				return 0;
			}
			fprintf(stderr, "could not read: %s\n",
				strerror(errno));
			exit(1);
		}
		buf[rr] = 0;
	}
	return 1;
}

/** write to ssl or fd, fatalexit on error */
static void
remote_write(SSL* ssl, int fd, const char* buf, size_t len)
{
	if(ssl) {
#ifdef HAVE_SSL
		if(SSL_write(ssl, buf, (int)len) <= 0)
			ssl_err("could not SSL_write");
#endif /* HAVE_SSL */
	} else {
		if(write(fd, buf, len) < (ssize_t)len) {
			fprintf(stderr, "could not write: %s\n",
				strerror(errno));
			exit(1);
		}
	}
}

/** send stdin to server */
static void
send_file(SSL* ssl, int fd, FILE* in, char* buf, size_t sz)
{
	char e[] = {0x04, 0x0a};
	while(fgets(buf, (int)sz, in)) {
		remote_write(ssl, fd, buf, strlen(buf));
	}
	/* send end-of-file marker */
	remote_write(ssl, fd, e, sizeof(e));
}

/** send command and display result */
static int
go_cmd(SSL* ssl, int fd, int argc, char* argv[])
{
	char pre[10];
	const char* space=" ";
	const char* newline="\n";
	int was_error = 0, first_line = 1;
	int i;
	char buf[1024];
	snprintf(pre, sizeof(pre), "NSDCT%d ", NSD_CONTROL_VERSION);
	remote_write(ssl, fd, pre, strlen(pre));
	for(i=0; i<argc; i++) {
		remote_write(ssl, fd, space, strlen(space));
		remote_write(ssl, fd, argv[i], strlen(argv[i]));
	}
	remote_write(ssl, fd, newline, strlen(newline));

	/* send contents to server */
	if(argc == 1 && (strcmp(argv[0], "addzones") == 0 ||
		strcmp(argv[0], "delzones") == 0)) {
		send_file(ssl, fd, stdin, buf, sizeof(buf));
	}

	while(1) {
		if(remote_read(ssl, fd, buf, sizeof(buf)) == 0) {
			break; /* EOF */
		}
		printf("%s", buf);
		if(first_line && strncmp(buf, "error", 5) == 0)
			was_error = 1;
		first_line = 0;
	}
	return was_error;
}

/** go ahead and read config, contact server and perform command and display */
static int
go(const char* cfgfile, char* svr, int argc, char* argv[])
{
	struct nsd_options* opt;
	int fd, ret;
#ifdef HAVE_SSL
	SSL_CTX* ctx = NULL;
#endif
	SSL* ssl = NULL;

	/* read config */
	if(!(opt = nsd_options_create(region_create(xalloc, free)))) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	tsig_init(opt->region);
	if(!parse_options_file(opt, cfgfile, NULL, NULL, NULL)) {
		fprintf(stderr, "could not read config file\n");
		exit(1);
	}
	if(!opt->control_enable)
		fprintf(stderr, "warning: control-enable is 'no' in the config file.\n");
	resolve_interface_names(opt);
#ifdef HAVE_SSL
	ctx = setup_ctx(opt);
#else
	if(options_remote_is_address(opt)) {
		fprintf(stderr, "error: NSD was compiled without SSL.\n");
		exit(1);
	}
#endif /* HAVE_SSL */

	/* contact server */
	fd = contact_server(svr, opt, argc>0&&strcmp(argv[0],"status")==0);
#ifdef HAVE_SSL
	ssl = setup_ssl(ctx, fd);
#endif

	/* send command */
	ret = go_cmd(ssl, fd, argc, argv);

#ifdef HAVE_SSL
	if(ssl) SSL_free(ssl);
#endif
	close(fd);
#ifdef HAVE_SSL
	if(ctx) SSL_CTX_free(ctx);
#endif
	region_destroy(opt->region);
	return ret;
}

/** getopt global, in case header files fail to declare it. */
extern int optind;
/** getopt global, in case header files fail to declare it. */
extern char* optarg;

/** Main routine for nsd-control */
int main(int argc, char* argv[])
{
	int c;
	const char* cfgfile = CONFIGFILE;
	char* svr = NULL;
	log_init("nsd-control");

#ifdef HAVE_SSL
#ifdef HAVE_ERR_LOAD_CRYPTO_STRINGS
	ERR_load_crypto_strings();
#endif
#if defined(HAVE_ERR_LOAD_SSL_STRINGS) && !defined(DEPRECATED_ERR_LOAD_SSL_STRINGS)
	ERR_load_SSL_strings();
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_CRYPTO)
	OpenSSL_add_all_algorithms();
#else
	OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS
		| OPENSSL_INIT_ADD_ALL_DIGESTS
		| OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || !defined(HAVE_OPENSSL_INIT_SSL)
	(void)SSL_library_init();
#else
	OPENSSL_init_ssl(0, NULL);
#endif

	if(!RAND_status()) {
                /* try to seed it */
                unsigned char buf[256];
                unsigned int v, seed=(unsigned)time(NULL) ^ (unsigned)getpid();
                size_t i;
		v = seed;
                for(i=0; i<256/sizeof(v); i++) {
                        memmove(buf+i*sizeof(v), &v, sizeof(v));
                        v = v*seed + (unsigned int)i;
                }
                RAND_seed(buf, 256);
		fprintf(stderr, "warning: no entropy, seeding openssl PRNG with time\n");
	}
#endif /* HAVE_SSL */

	/* parse the options */
	while( (c=getopt(argc, argv, "c:s:h")) != -1) {
		switch(c) {
		case 'c':
			cfgfile = optarg;
			break;
		case 's':
			svr = optarg;
			break;
		case '?':
		case 'h':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if(argc == 0)
		usage();
	if(argc >= 1 && strcmp(argv[0], "start")==0) {
		const char *path;
		if((path = getenv("NSD_PATH")) == NULL) {
			path = NSD_START_PATH;
		}
		if(execl(path, "nsd", "-c", cfgfile, (char*)NULL) < 0) {
			fprintf(stderr, "could not exec %s: %s\n",
				NSD_START_PATH, strerror(errno));
			exit(1);
		}
	}

	return go(cfgfile, svr, argc, argv);
}
