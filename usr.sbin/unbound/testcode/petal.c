/*
 * petal.c - https daemon that is small and beautiful.
 *
 * Copyright (c) 2010, NLnet Labs. All rights reserved.
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
 * HTTP1.1/SSL server.
 */

#include "config.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#ifdef HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif
#ifdef HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif
#ifdef HAVE_OPENSSL_RAND_H
#include <openssl/rand.h>
#endif
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <ctype.h>
#include <signal.h>
#if defined(UNBOUND_ALLOC_LITE) || defined(UNBOUND_ALLOC_STATS)
#ifdef malloc
#undef malloc
#endif
#ifdef free
#undef free
#endif
#endif /* alloc lite or alloc stats */

/** verbosity for this application */
static int verb = 0;

/** Give petal usage, and exit (1). */
static void
usage(void)
{
	printf("Usage:	petal [opts]\n");
	printf("	https daemon serves files from ./'host'/filename\n");
	printf("	(no hostname: from the 'default' directory)\n");
	printf("-a addr		bind to this address, 127.0.0.1\n");
	printf("-p port		port number, default 443\n");
	printf("-k keyfile	SSL private key file (PEM), petal.key\n");
	printf("-c certfile	SSL certificate file (PEM), petal.pem\n");
	printf("-v		more verbose\n");
	printf("-h		show this usage help\n");
	printf("Version %s\n", PACKAGE_VERSION);
	printf("BSD licensed, see LICENSE in source package for details.\n");
	printf("Report bugs to %s\n", PACKAGE_BUGREPORT);
	exit(1);
}

/** fatal exit */
static void print_exit(const char* str) {printf("error %s\n", str); exit(1);}
/** print errno */
static void log_errno(const char* str)
{printf("error %s: %s\n", str, strerror(errno));}

/** parse a text IP address into a sockaddr */
static int
parse_ip_addr(char* str, int port, struct sockaddr_storage* ret, socklen_t* l)
{
	socklen_t len = 0;
	struct sockaddr_storage* addr = NULL;
	struct sockaddr_in6 a6;
	struct sockaddr_in a;
	uint16_t p = (uint16_t)port;
	int fam = 0;
	memset(&a6, 0, sizeof(a6));
	memset(&a, 0, sizeof(a));

	if(inet_pton(AF_INET6, str, &a6.sin6_addr) > 0) {
		/* it is an IPv6 */
		fam = AF_INET6;
		a6.sin6_family = AF_INET6;
		a6.sin6_port = (in_port_t)htons(p);
		addr = (struct sockaddr_storage*)&a6;
		len = (socklen_t)sizeof(struct sockaddr_in6);
	}
	if(inet_pton(AF_INET, str, &a.sin_addr) > 0) {
		/* it is an IPv4 */
		fam = AF_INET;
		a.sin_family = AF_INET;
		a.sin_port = (in_port_t)htons(p);
		addr = (struct sockaddr_storage*)&a;
		len = (socklen_t)sizeof(struct sockaddr_in);
	}
	if(!len) print_exit("cannot parse addr");
	*l = len;
	memmove(ret, addr, len);
	return fam;
}

/** close the fd */
static void
fd_close(int fd)
{
#ifndef USE_WINSOCK
	close(fd);
#else
	closesocket(fd);
#endif
}

/** 
 * Read one line from SSL
 * zero terminates.
 * skips "\r\n" (but not copied to buf).
 * @param ssl: the SSL connection to read from (blocking).
 * @param buf: buffer to return line in.
 * @param len: size of the buffer.
 * @return 0 on error, 1 on success.
 */
static int
read_ssl_line(SSL* ssl, char* buf, size_t len)
{
	size_t n = 0;
	int r;
	int endnl = 0;
	while(1) {
		if(n >= len) {
			if(verb) printf("line too long\n");
			return 0;
		}
		if((r = SSL_read(ssl, buf+n, 1)) <= 0) {
			if(SSL_get_error(ssl, r) == SSL_ERROR_ZERO_RETURN) {
				/* EOF */
				break;
			}
			if(verb) printf("could not SSL_read\n");
			return 0;
		}
		if(endnl && buf[n] == '\n') {
			break;
		} else if(endnl) {
			/* bad data */
			if(verb) printf("error: stray linefeeds\n");
			return 0;
		} else if(buf[n] == '\r') {
			/* skip \r, and also \n on the wire */
			endnl = 1;
			continue;
		} else if(buf[n] == '\n') {
			/* skip the \n, we are done */
			break;
		} else n++;
	}
	buf[n] = 0;
	return 1;
}

/** process one http header */
static int
process_one_header(char* buf, char* file, size_t flen, char* host, size_t hlen,
	int* vs)
{
	if(strncasecmp(buf, "GET ", 4) == 0) {
		char* e = strstr(buf, " HTTP/1.1");
		if(!e) e = strstr(buf, " http/1.1");
		if(!e) {
			e = strstr(buf, " HTTP/1.0");
			if(!e) e = strstr(buf, " http/1.0");
			if(!e) e = strrchr(buf, ' ');
			if(!e) e = strrchr(buf, '\t');
			if(e) *vs = 10;
		}
		if(e) *e = 0;
		if(strlen(buf) < 4) return 0;
		(void)strlcpy(file, buf+4, flen);
	} else if(strncasecmp(buf, "Host: ", 6) == 0) {
		(void)strlcpy(host, buf+6, hlen);
	}
	return 1;
}

/** read http headers and process them */
static int
read_http_headers(SSL* ssl, char* file, size_t flen, char* host, size_t hlen,
	int* vs)
{
	char buf[1024];
	file[0] = 0;
	host[0] = 0;
	while(read_ssl_line(ssl, buf, sizeof(buf))) {
		if(verb>=2) printf("read: %s\n", buf);
		if(buf[0] == 0) {
			int e = ERR_peek_error();
			printf("error string: %s\n", ERR_reason_error_string(e));
			return 1;
		}
		if(!process_one_header(buf, file, flen, host, hlen, vs))
			return 0;
	}
	return 0;
}

/** setup SSL context */
static SSL_CTX*
setup_ctx(char* key, char* cert)
{
	SSL_CTX* ctx = SSL_CTX_new(SSLv23_server_method());
	if(!ctx) print_exit("out of memory");
#if SSL_OP_NO_SSLv2 != 0
	(void)SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2);
#endif
	(void)SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv3);
#ifdef HAVE_SSL_CTX_SET_SECURITY_LEVEL
	SSL_CTX_set_security_level(ctx, 0); /* for keys in tests */
#endif
	if(!SSL_CTX_use_certificate_chain_file(ctx, cert)) {
		int e = ERR_peek_error();
		printf("error string: %s\n", ERR_reason_error_string(e));
		print_exit("cannot read cert");
	}
	if(!SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM))
		print_exit("cannot read key");
	if(!SSL_CTX_check_private_key(ctx))
		print_exit("private key is not correct");
#if HAVE_DECL_SSL_CTX_SET_ECDH_AUTO
	if (!SSL_CTX_set_ecdh_auto(ctx,1))
		if(verb>=1) printf("failed to set_ecdh_auto, not enabling ECDHE\n");
#elif defined(USE_ECDSA) && defined(HAVE_SSL_CTX_SET_TMP_ECDH)
	if(1) {
		EC_KEY *ecdh = EC_KEY_new_by_curve_name (NID_X9_62_prime256v1);
		if (!ecdh) {
			if(verb>=1) printf("could not find p256, not enabling ECDHE\n");
		} else {
			if (1 != SSL_CTX_set_tmp_ecdh (ctx, ecdh)) {
				if(verb>=1) printf("Error in SSL_CTX_set_tmp_ecdh, not enabling ECDHE\n");
			}
			EC_KEY_free(ecdh);
		}
	}
#endif
	if(!SSL_CTX_load_verify_locations(ctx, cert, NULL))
		print_exit("cannot load cert verify locations");
	return ctx;
}

/** setup listening TCP */
static int
setup_fd(char* addr, int port)
{
	struct sockaddr_storage ad;
	socklen_t len;
	int fd;
	int c = 1;
	int fam = parse_ip_addr(addr, port, &ad, &len);
	fd = socket(fam, SOCK_STREAM, 0);
	if(fd == -1) {
		log_errno("socket");
		return -1;
	}
	if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
		(void*)&c, (socklen_t) sizeof(int)) < 0) {
		log_errno("setsockopt(SOL_SOCKET, SO_REUSEADDR)");
	}
	if(bind(fd, (struct sockaddr*)&ad, len) == -1) {
		log_errno("bind");
		fd_close(fd);
		return -1;
	}
	if(listen(fd, 5) == -1) {
		log_errno("listen");
		fd_close(fd);
		return -1;
	}
	return fd;
}

/** setup SSL connection to the client */
static SSL*
setup_ssl(int s, SSL_CTX* ctx)
{
	SSL* ssl = SSL_new(ctx);
	if(!ssl) return NULL;
	SSL_set_accept_state(ssl);
	(void)SSL_set_mode(ssl, (long)SSL_MODE_AUTO_RETRY);
	if(!SSL_set_fd(ssl, s)) {
		SSL_free(ssl);
		return NULL;
	}
	return ssl;
}

/** check a file name for safety */
static int
file_name_is_safe(char* s)
{
	size_t l = strlen(s);
	if(s[0] != '/')
		return 0; /* must start with / */
	if(strstr(s, "/../"))
		return 0; /* no updirs in URL */
	if(l>=3 && s[l-1]=='.' && s[l-2]=='.' && s[l-3]=='/')
		return 0; /* ends with /.. */
	return 1;
}

/** adjust host */
static void
adjust_host(char* host)
{
	size_t i, len;
	/* remove a port number if present */
	if(strrchr(host, ':'))
		*strrchr(host, ':') = 0;
	/* lowercase */
	len = strlen(host);
	for(i=0; i<len; i++)
		host[i] = tolower((unsigned char)host[i]);
}

/** adjust filename */
static void
adjust_file(char* file)
{
	size_t i, len;
	len = strlen(file);
	for(i=0; i<len; i++)
		file[i] = tolower((unsigned char)file[i]);
}

/** check a host name for safety */
static int
host_name_is_safe(char* s)
{
	if(strchr(s, '/'))
		return 0;
	if(strcmp(s, "..") == 0)
		return 0;
	if(strcmp(s, ".") == 0)
		return 0;
	return 1;
}

/** provide file in whole transfer */
static void
provide_file_10(SSL* ssl, char* fname)
{
	char* buf, *at;
	size_t len, avail, header_reserve=1024;
	FILE* in = fopen(fname, 
#ifndef USE_WINSOCK
		"r"
#else
		"rb"
#endif
		);
	size_t r;
	const char* rcode = "200 OK";
	if(!in) {
		char hdr[1024];
		rcode = "404 File not found";
		snprintf(hdr, sizeof(hdr), "HTTP/1.1 %s\r\n\r\n", rcode);
		r = strlen(hdr);
		if(SSL_write(ssl, hdr, (int)r) <= 0) {
			/* write failure */
		}
		return;
	}
	fseek(in, 0, SEEK_END);
	len = (size_t)ftell(in);
	fseek(in, 0, SEEK_SET);
	/* plus some space for the header */
	buf = (char*)malloc(len+header_reserve);
	if(!buf) {
		fclose(in);
		return;
	}
	avail = len+header_reserve;
	at = buf;
	snprintf(at, avail, "HTTP/1.1 %s\r\n", rcode);
	r = strlen(at);
	at += r;
	avail -= r;
	snprintf(at, avail, "Server: petal/%s\r\n", PACKAGE_VERSION);
	r = strlen(at);
	at += r;
	avail -= r;
	snprintf(at, avail, "Content-Length: %u\r\n", (unsigned)len);
	r = strlen(at);
	at += r;
	avail -= r;
	snprintf(at, avail, "\r\n");
	r = strlen(at);
	at += r;
	avail -= r;
	if(avail < len) { /* robust */
		free(buf);
		fclose(in);
		return;
	}
	if(fread(at, 1, len, in) != len) {
		free(buf);
		fclose(in);
		return;
	}
	fclose(in);
	at += len;
	/* avail -= len; unused */
	if(SSL_write(ssl, buf, at-buf) <= 0) {
		/* write failure */
	}
	free(buf);
}

/** provide file over SSL, chunked encoding */
static void
provide_file_chunked(SSL* ssl, char* fname)
{
	char buf[16384];
	char* tmpbuf = NULL;
	char* at = buf;
	size_t avail = sizeof(buf);
	size_t r;
	FILE* in = fopen(fname, 
#ifndef USE_WINSOCK
		"r"
#else
		"rb"
#endif
		);
	const char* rcode = "200 OK";
	if(!in) {
		rcode = "404 File not found";
	}

	/* print headers */
	snprintf(at, avail, "HTTP/1.1 %s\r\n", rcode);
	r = strlen(at);
	at += r;
	avail -= r;
	snprintf(at, avail, "Server: petal/%s\r\n", PACKAGE_VERSION);
	r = strlen(at);
	at += r;
	avail -= r;
	snprintf(at, avail, "Transfer-Encoding: chunked\r\n");
	r = strlen(at);
	at += r;
	avail -= r;
	snprintf(at, avail, "Connection: close\r\n");
	r = strlen(at);
	at += r;
	avail -= r;
	snprintf(at, avail, "\r\n");
	r = strlen(at);
	at += r;
	avail -= r;
	if(avail < 16) { /* robust */
		if(in) fclose(in);
		return;
	}

	do {
		size_t red;
		free(tmpbuf);
		tmpbuf = malloc(avail-16);
		if(!tmpbuf)
			break;
		/* read chunk; space-16 for xxxxCRLF..CRLF0CRLFCRLF (3 spare)*/
		red = in?fread(tmpbuf, 1, avail-16, in):0;
		/* prepare chunk */
		snprintf(at, avail, "%x\r\n", (unsigned)red);
		r = strlen(at);
		if(verb >= 3)
		{printf("chunk len %x\n", (unsigned)red); fflush(stdout);}
		at += r;
		avail -= r;
		if(red != 0) {
			if(red > avail) break; /* robust */
			memmove(at, tmpbuf, red);
			at += red;
			avail -= red;
			snprintf(at, avail, "\r\n");
			r = strlen(at);
			at += r;
			avail -= r;
		}
		if(in && feof(in) && red != 0) {
			snprintf(at, avail, "0\r\n");
			r = strlen(at);
			at += r;
			avail -= r;
		}
		if(!in || feof(in)) {
			snprintf(at, avail, "\r\n");
			r = strlen(at);
			at += r;
			/* avail -= r; unused */
		}
		/* send chunk */
		if(SSL_write(ssl, buf, at-buf) <= 0) {
			/* SSL error */
			break;
		}

		/* setup for next chunk */
		at = buf;
		avail = sizeof(buf);
	} while(in && !feof(in) && !ferror(in));

	free(tmpbuf);
	if(in) fclose(in);
}

/** provide service to the ssl descriptor */
static void
service_ssl(SSL* ssl, struct sockaddr_storage* from, socklen_t falen)
{
	char file[1024];
	char host[1024];
	char combined[2048];
	int vs = 11;
	if(!read_http_headers(ssl, file, sizeof(file), host, sizeof(host),
		&vs))
		return;
	if(host[0] != 0) adjust_host(host);
	if(file[0] != 0) adjust_file(file);
	if(host[0] == 0 || !host_name_is_safe(host))
		(void)strlcpy(host, "default", sizeof(host));
	if(!file_name_is_safe(file)) {
		return;
	}
	snprintf(combined, sizeof(combined), "%s%s", host, file);
	if(verb) {
		char out[100];
		void* a = &((struct sockaddr_in*)from)->sin_addr;
		if(falen != (socklen_t)sizeof(struct sockaddr_in))
			a = &((struct sockaddr_in6*)from)->sin6_addr;
		out[0]=0;
		(void)inet_ntop((int)((struct sockaddr_in*)from)->sin_family,
			a, out, (socklen_t)sizeof(out));
		printf("%s requests %s\n", out, combined);
		fflush(stdout);
	}
	if(vs == 10)
		provide_file_10(ssl, combined);
	else	provide_file_chunked(ssl, combined);
}

/** provide ssl service */
static void
do_service(char* addr, int port, char* key, char* cert)
{
	SSL_CTX* sslctx = setup_ctx(key, cert);
	int fd = setup_fd(addr, port);
	if(fd == -1) print_exit("could not setup sockets");
	if(verb) {printf("petal start\n"); fflush(stdout);}
	while(1) {
		struct sockaddr_storage from;
		socklen_t flen = (socklen_t)sizeof(from);
		int s;
		memset(&from, 0, sizeof(from));
		s = accept(fd, (struct sockaddr*)&from, &flen);
		if(verb) fflush(stdout);
		if(s != -1) {
			SSL* ssl = setup_ssl(s, sslctx);
			if(verb) fflush(stdout);
			if(ssl) {
				service_ssl(ssl, &from, flen);
				if(verb) fflush(stdout);
				SSL_shutdown(ssl);
				SSL_free(ssl);
			}
			fd_close(s);
		} else if (verb >=2) log_errno("accept");
		if(verb) fflush(stdout);
	}
	/* if we get a kill signal, the process dies and the OS reaps us */
	if(verb) printf("petal end\n");
	fd_close(fd);
	SSL_CTX_free(sslctx);
}

/** getopt global, in case header files fail to declare it. */
extern int optind;
/** getopt global, in case header files fail to declare it. */
extern char* optarg;

/** Main routine for petal */
int main(int argc, char* argv[])
{
	int c;
	int port = 443;
	char* addr = "127.0.0.1", *key = "petal.key", *cert = "petal.pem";
#ifdef USE_WINSOCK
	WSADATA wsa_data;
	if((c=WSAStartup(MAKEWORD(2,2), &wsa_data)) != 0)
	{	printf("WSAStartup failed\n"); exit(1); }
	atexit((void (*)(void))WSACleanup);
#endif

	/* parse the options */
	while( (c=getopt(argc, argv, "a:c:k:hp:v")) != -1) {
		switch(c) {
		case 'a':
			addr = optarg;
			break;
		case 'c':
			cert = optarg;
			break;
		case 'k':
			key = optarg;
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 'v':
			verb++;
			break;
		case '?':
		case 'h':
		default:
			usage();
		}
	}
	argc -= optind;
	/* argv += optind; not using further arguments */
	if(argc != 0)
		usage();

#ifdef SIGPIPE
	(void)signal(SIGPIPE, SIG_IGN);
#endif
#ifdef HAVE_ERR_LOAD_CRYPTO_STRINGS
	ERR_load_crypto_strings();
#endif
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

	do_service(addr, port, key, cert);

#ifdef HAVE_CRYPTO_CLEANUP_ALL_EX_DATA
	CRYPTO_cleanup_all_ex_data();
#endif
#ifdef HAVE_ERR_FREE_STRINGS
	ERR_free_strings();
#endif
	return 0;
}
