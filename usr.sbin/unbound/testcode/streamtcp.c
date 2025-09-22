/*
 * testcode/streamtcp.c - debug program perform multiple DNS queries on tcp.
 *
 * Copyright (c) 2008, NLnet Labs. All rights reserved.
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
 * This program performs multiple DNS queries on a TCP stream.
 */

#include "config.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include "util/locks.h"
#include "util/log.h"
#include "util/net_help.h"
#include "util/proxy_protocol.h"
#include "util/data/msgencode.h"
#include "util/data/msgparse.h"
#include "util/data/msgreply.h"
#include "util/data/dname.h"
#include "sldns/sbuffer.h"
#include "sldns/str2wire.h"
#include "sldns/wire2str.h"
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>

#ifndef PF_INET6
/** define in case streamtcp is compiled on legacy systems */
#define PF_INET6 10
#endif

/** usage information for streamtcp */
static void usage(char* argv[])
{
	printf("usage: %s [options] name type class ...\n", argv[0]);
	printf("	sends the name-type-class queries over TCP.\n");
	printf("-f server	what ipaddr@portnr to send the queries to\n");
	printf("-p client	what ipaddr@portnr to include in PROXYv2\n");
	printf("-u 		use UDP. No retries are attempted.\n");
	printf("-n 		do not wait for an answer.\n");
	printf("-a 		print answers as they arrive.\n");
	printf("-d secs		delay after connection before sending query\n");
	printf("-s		use ssl\n");
	printf("-h 		this help text\n");
	printf("IXFR=N 		for the type, sends ixfr query with serial N.\n");
	printf("NOTIFY[=N] 	for the type, sends notify. Can set new zone serial N.\n");
	exit(1);
}

/** open TCP socket to svr */
static int
open_svr(const char* svr, int udp, struct sockaddr_storage* addr,
	socklen_t* addrlen)
{
	int fd = -1;
	/* svr can be ip@port */
	memset(addr, 0, sizeof(*addr));
	if(!extstrtoaddr(svr, addr, addrlen, UNBOUND_DNS_PORT)) {
		printf("fatal: bad server specs '%s'\n", svr);
		exit(1);
	}
	fd = socket(addr_is_ip6(addr, *addrlen)?PF_INET6:PF_INET,
		udp?SOCK_DGRAM:SOCK_STREAM, 0);
	if(fd == -1) {
#ifndef USE_WINSOCK
		perror("socket() error");
#else
		printf("socket: %s\n", wsa_strerror(WSAGetLastError()));
#endif
		exit(1);
	}
	if(connect(fd, (struct sockaddr*)addr, *addrlen) < 0) {
#ifndef USE_WINSOCK
		perror("connect() error");
#else
		printf("connect: %s\n", wsa_strerror(WSAGetLastError()));
#endif
		exit(1);
	}
	return fd;
}

/** Append a SOA record with serial number */
static void
write_soa_serial_to_buf(sldns_buffer* buf, struct query_info* qinfo,
	uint32_t serial)
{
	sldns_buffer_set_position(buf, sldns_buffer_limit(buf));
	sldns_buffer_set_limit(buf, sldns_buffer_capacity(buf));
	/* Write compressed reference to the query */
	sldns_buffer_write_u16(buf, PTR_CREATE(LDNS_HEADER_SIZE));
	sldns_buffer_write_u16(buf, LDNS_RR_TYPE_SOA);
	sldns_buffer_write_u16(buf, qinfo->qclass);
	sldns_buffer_write_u32(buf, 3600); /* TTL */
	sldns_buffer_write_u16(buf, 1+1+4*5); /* rdatalen */
	sldns_buffer_write_u8(buf, 0); /* primary "." */
	sldns_buffer_write_u8(buf, 0); /* email "." */
	sldns_buffer_write_u32(buf, serial); /* serial */
	sldns_buffer_write_u32(buf, 0); /* refresh */
	sldns_buffer_write_u32(buf, 0); /* retry */
	sldns_buffer_write_u32(buf, 0); /* expire */
	sldns_buffer_write_u32(buf, 0); /* minimum */
	sldns_buffer_flip(buf);
}

/** write a query over the TCP fd */
static void
write_q(int fd, int udp, SSL* ssl, sldns_buffer* buf, uint16_t id,
	sldns_buffer* proxy_buf, int pp2_parsed,
	const char* strname, const char* strtype, const char* strclass)
{
	struct query_info qinfo;
	size_t proxy_buf_limit = sldns_buffer_limit(proxy_buf);
	int have_serial = 0, is_notify = 0;
	uint32_t serial = 0;
	/* qname */
	qinfo.qname = sldns_str2wire_dname(strname, &qinfo.qname_len);
	if(!qinfo.qname) {
		printf("cannot parse query name: '%s'\n", strname);
		exit(1);
	}

	/* qtype */
	if(strncasecmp(strtype, "IXFR=", 5) == 0) {
		serial = (uint32_t)atoi(strtype+5);
		have_serial = 1;
		qinfo.qtype = LDNS_RR_TYPE_IXFR;
	} else if(strcasecmp(strtype, "NOTIFY") == 0) {
		is_notify = 1;
		qinfo.qtype = LDNS_RR_TYPE_SOA;
	} else if(strncasecmp(strtype, "NOTIFY=", 7) == 0) {
		serial = (uint32_t)atoi(strtype+7);
		have_serial = 1;
		is_notify = 1;
		qinfo.qtype = LDNS_RR_TYPE_SOA;
	} else {
		qinfo.qtype = sldns_get_rr_type_by_name(strtype);
		if(qinfo.qtype == 0 && strcmp(strtype, "TYPE0") != 0) {
			printf("cannot parse query type: '%s'\n", strtype);
			exit(1);
		}
	}
	/* qclass */
	qinfo.qclass = sldns_get_rr_class_by_name(strclass);
	if(qinfo.qclass == 0 && strcmp(strclass, "CLASS0") != 0) {
		printf("cannot parse query class: '%s'\n", strclass);
		exit(1);
	}

	/* clear local alias */
	qinfo.local_alias = NULL;

	/* make query */
	qinfo_query_encode(buf, &qinfo);
	sldns_buffer_write_u16_at(buf, 0, id);
	sldns_buffer_write_u16_at(buf, 2, BIT_RD);

	if(have_serial && qinfo.qtype == LDNS_RR_TYPE_IXFR) {
		/* Attach serial to SOA record in the authority section. */
		write_soa_serial_to_buf(buf, &qinfo, serial);
		LDNS_NSCOUNT_SET(sldns_buffer_begin(buf), 1);
	}
	if(is_notify) {
		LDNS_OPCODE_SET(sldns_buffer_begin(buf), LDNS_PACKET_NOTIFY);
		LDNS_RD_CLR(sldns_buffer_begin(buf));
		LDNS_AA_SET(sldns_buffer_begin(buf));
		if(have_serial) {
			write_soa_serial_to_buf(buf, &qinfo, serial);
			LDNS_ANCOUNT_SET(sldns_buffer_begin(buf), 1);
		}
	}

	if(1) {
		/* add EDNS DO */
		struct edns_data edns;
		memset(&edns, 0, sizeof(edns));
		edns.edns_present = 1;
		edns.bits = EDNS_DO;
		edns.udp_size = 4096;
		if(sldns_buffer_capacity(buf) >=
			sldns_buffer_limit(buf)+calc_edns_field_size(&edns))
			attach_edns_record(buf, &edns);
	}

	/* we need to send the PROXYv2 information in every UDP message */
	if(udp && pp2_parsed) {
		/* append the proxy_buf with the buf's content
		 * and use that for sending */
		if(sldns_buffer_capacity(proxy_buf) <
			sldns_buffer_limit(proxy_buf) +
			sldns_buffer_limit(buf)) {
			printf("buffer too small for packet + proxy");
			exit(1);
		}
		sldns_buffer_clear(proxy_buf);
		sldns_buffer_skip(proxy_buf, proxy_buf_limit);
		sldns_buffer_write(proxy_buf, sldns_buffer_begin(buf),
			sldns_buffer_limit(buf));
		sldns_buffer_flip(proxy_buf);
		buf = proxy_buf;
	}

	/* send it */
	if(!udp) {
		uint16_t len = (uint16_t)sldns_buffer_limit(buf);
		len = htons(len);
		if(ssl) {
			if(SSL_write(ssl, (void*)&len, (int)sizeof(len)) <= 0) {
				log_crypto_err("cannot SSL_write");
				exit(1);
			}
		} else {
			if(send(fd, (void*)&len, sizeof(len), 0) <
				(ssize_t)sizeof(len)){
#ifndef USE_WINSOCK
				perror("send() len failed");
#else
				printf("send len: %s\n",
					wsa_strerror(WSAGetLastError()));
#endif
				exit(1);
			}
		}
	}
	if(ssl) {
		if(SSL_write(ssl, (void*)sldns_buffer_begin(buf),
			(int)sldns_buffer_limit(buf)) <= 0) {
			log_crypto_err("cannot SSL_write");
			exit(1);
		}
	} else {
		if(send(fd, (void*)sldns_buffer_begin(buf),
			sldns_buffer_limit(buf), 0) <
			(ssize_t)sldns_buffer_limit(buf)) {
#ifndef USE_WINSOCK
			perror("send() data failed");
#else
			printf("send data: %s\n",
				wsa_strerror(WSAGetLastError()));
#endif
			exit(1);
		}
	}

	/* reset the proxy_buf for next packet */
	sldns_buffer_set_limit(proxy_buf, proxy_buf_limit);
	free(qinfo.qname);
}

/** receive DNS datagram over TCP and print it */
static void
recv_one(int fd, int udp, SSL* ssl, sldns_buffer* buf)
{
	size_t i;
	char* pktstr;
	uint16_t len;
	if(!udp) {
		if(ssl) {
			int sr = SSL_read(ssl, (void*)&len, (int)sizeof(len));
			if(sr == 0) {
				printf("ssl: stream closed\n");
				exit(1);
			}
			if(sr < 0) {
				log_crypto_err("could not SSL_read");
				exit(1);
			}
		} else {
			ssize_t r = recv(fd, (void*)&len, sizeof(len), 0);
			if(r == 0) {
				printf("recv: stream closed\n");
				exit(1);
			}	
			if(r < (ssize_t)sizeof(len)) {
#ifndef USE_WINSOCK
				perror("read() len failed");
#else
				printf("read len: %s\n",
					wsa_strerror(WSAGetLastError()));
#endif
				exit(1);
			}
		}
		len = ntohs(len);
		sldns_buffer_clear(buf);
		sldns_buffer_set_limit(buf, len);
		if(ssl) {
			int r = SSL_read(ssl, (void*)sldns_buffer_begin(buf),
				(int)len);
			if(r <= 0) {
				log_crypto_err("could not SSL_read");
				exit(1);
			}
			if(r != (int)len)
				fatal_exit("ssl_read %d of %d", r, len);
		} else {
			if(recv(fd, (void*)sldns_buffer_begin(buf), len, 0) <
				(ssize_t)len) {
#ifndef USE_WINSOCK
				perror("read() data failed");
#else
				printf("read data: %s\n",
					wsa_strerror(WSAGetLastError()));
#endif
				exit(1);
			}
		}
	} else {
		ssize_t l;
		sldns_buffer_clear(buf);
		if((l=recv(fd, (void*)sldns_buffer_begin(buf),
			sldns_buffer_capacity(buf), 0)) < 0) {
#ifndef USE_WINSOCK
			perror("read() data failed");
#else
			printf("read data: %s\n",
				wsa_strerror(WSAGetLastError()));
#endif
			exit(1);
		}
		sldns_buffer_set_limit(buf, (size_t)l);
		len = (size_t)l;
	}
	printf("\nnext received packet\n");
	printf("data[%d] ", (int)sldns_buffer_limit(buf));
	for(i=0; i<sldns_buffer_limit(buf); i++) {
		const char* hex = "0123456789ABCDEF";
		printf("%c%c", hex[(sldns_buffer_read_u8_at(buf, i)&0xf0)>>4],
                        hex[sldns_buffer_read_u8_at(buf, i)&0x0f]);
	}
	printf("\n");

	pktstr = sldns_wire2str_pkt(sldns_buffer_begin(buf), len);
	printf("%s", pktstr);
	free(pktstr);
}

/** see if we can receive any results */
static void
print_any_answers(int fd, int udp, SSL* ssl, sldns_buffer* buf,
	int* num_answers, int wait_all)
{
	/* see if the fd can read, if so, print one answer, repeat */
	int ret;
	struct timeval tv, *waittv;
	fd_set rfd;
	while(*num_answers > 0) {
		memset(&rfd, 0, sizeof(rfd));
		memset(&tv, 0, sizeof(tv));
		FD_ZERO(&rfd);
		FD_SET(fd, &rfd);
		if(wait_all) waittv = NULL;
		else waittv = &tv;
		ret = select(fd+1, &rfd, NULL, NULL, waittv);
		if(ret < 0) {
			if(errno == EINTR || errno == EAGAIN) continue;
			perror("select() failed");
			exit(1);
		}
		if(ret == 0) {
			if(wait_all) continue;
			return;
		}
		(*num_answers) -= 1;
		recv_one(fd, udp, ssl, buf);
	}
}

static int get_random(void)
{
	int r;
	if (RAND_bytes((unsigned char*)&r, (int)sizeof(r)) == 1) {
		return r;
	}
	return (int)arc4random();
}

/* parse the pp2_client and populate the proxy_buffer
 * It doesn't populate the destination parts. */
static int parse_pp2_client(const char* pp2_client, int udp,
	sldns_buffer* proxy_buf)
{
	struct sockaddr_storage pp2_addr;
	size_t bytes_written;
	socklen_t pp2_addrlen = 0;
	memset(&pp2_addr, 0, sizeof(pp2_addr));
	if(*pp2_client == 0) return 0;
	if(!extstrtoaddr(pp2_client, &pp2_addr, &pp2_addrlen, UNBOUND_DNS_PORT)) {
		printf("fatal: bad proxy client specs '%s'\n", pp2_client);
		exit(1);
	}
	sldns_buffer_clear(proxy_buf);
	bytes_written = pp2_write_to_buf(sldns_buffer_begin(proxy_buf),
		sldns_buffer_remaining(proxy_buf), &pp2_addr, !udp);
	sldns_buffer_set_position(proxy_buf, bytes_written);
	sldns_buffer_flip(proxy_buf);
	return 1;
}

/** send the TCP queries and print answers */
static void
send_em(const char* svr, const char* pp2_client, int udp, int usessl,
	int noanswer, int onarrival, int delay, int num, char** qs)
{
	struct sockaddr_storage svr_addr;
	socklen_t svr_addrlen;
	int fd = open_svr(svr, udp, &svr_addr, &svr_addrlen);
	int i, wait_results = 0, pp2_parsed;
	SSL_CTX* ctx = NULL;
	SSL* ssl = NULL;
	sldns_buffer* buf = sldns_buffer_new(65553);
	sldns_buffer* proxy_buf = sldns_buffer_new(65553);
	if(!buf || !proxy_buf) {
		sldns_buffer_free(buf);
		sldns_buffer_free(proxy_buf);
		fatal_exit("out of memory");
	}
	pp2_parsed = parse_pp2_client(pp2_client, udp, proxy_buf);
	if(usessl) {
		ctx = connect_sslctx_create(NULL, NULL, NULL, 0);
		if(!ctx) fatal_exit("cannot create ssl ctx");
		ssl = outgoing_ssl_fd(ctx, fd);
		if(!ssl) fatal_exit("cannot create ssl");
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
		if(1) {
#ifdef HAVE_SSL_GET1_PEER_CERTIFICATE
			X509* x = SSL_get1_peer_certificate(ssl);
#else
			X509* x = SSL_get_peer_certificate(ssl);
#endif
			if(!x) printf("SSL: no peer certificate\n");
			else {
				X509_print_fp(stdout, x);
				X509_free(x);
			}
		}
	}
	/* Send the PROXYv2 information once per stream */
	if(!udp && pp2_parsed) {
		if(ssl) {
			if(SSL_write(ssl, (void*)sldns_buffer_begin(proxy_buf),
				(int)sldns_buffer_limit(proxy_buf)) <= 0) {
				log_crypto_err("cannot SSL_write");
				exit(1);
			}
		} else {
			if(send(fd, (void*)sldns_buffer_begin(proxy_buf),
				sldns_buffer_limit(proxy_buf), 0) <
				(ssize_t)sldns_buffer_limit(proxy_buf)) {
#ifndef USE_WINSOCK
				perror("send() data failed");
#else
				printf("send data: %s\n",
					wsa_strerror(WSAGetLastError()));
#endif
				exit(1);
			}
		}
	}
	for(i=0; i<num; i+=3) {
		if (delay != 0) {
#ifdef HAVE_SLEEP
			sleep((unsigned)delay);
#else
			Sleep(delay*1000);
#endif
		}
		printf("\nNext query is %s %s %s\n", qs[i], qs[i+1], qs[i+2]);
		write_q(fd, udp, ssl, buf, (uint16_t)get_random(), proxy_buf,
			pp2_parsed,
			qs[i], qs[i+1], qs[i+2]);
		/* print at least one result */
		if(onarrival) {
			wait_results += 1; /* one more answer to fetch */
			print_any_answers(fd, udp, ssl, buf, &wait_results, 0);
		} else if(!noanswer) {
			recv_one(fd, udp, ssl, buf);
		}
	}
	if(onarrival)
		print_any_answers(fd, udp, ssl, buf, &wait_results, 1);

	if(usessl) {
		SSL_shutdown(ssl);
		SSL_free(ssl);
		SSL_CTX_free(ctx);
	}
	sock_close(fd);
	sldns_buffer_free(buf);
	sldns_buffer_free(proxy_buf);
	printf("orderly exit\n");
}

#ifdef SIGPIPE
/** SIGPIPE handler */
static RETSIGTYPE sigh(int sig)
{
	char str[] = "Got unhandled signal   \n";
	if(sig == SIGPIPE) {
		char* strpipe = "got SIGPIPE, remote connection gone\n";
		/* simple cast to void will not silence Wunused-result */
		(void)!write(STDOUT_FILENO, strpipe, strlen(strpipe));
		exit(1);
	}
	str[21] = '0' + (sig/10)%10;
	str[22] = '0' + sig%10;
	/* simple cast to void will not silence Wunused-result */
	(void)!write(STDOUT_FILENO, str, strlen(str));
	exit(1);
}
#endif /* SIGPIPE */

/** getopt global, in case header files fail to declare it. */
extern int optind;
/** getopt global, in case header files fail to declare it. */
extern char* optarg;

/** main program for streamtcp */
int main(int argc, char** argv)
{
	int c;
	const char* svr = "127.0.0.1";
	const char* pp2_client = "";
	int udp = 0;
	int noanswer = 0;
	int onarrival = 0;
	int usessl = 0;
	int delay = 0;

#ifdef USE_WINSOCK
	WSADATA wsa_data;
	if(WSAStartup(MAKEWORD(2,2), &wsa_data) != 0) {
		printf("WSAStartup failed\n");
		return 1;
	}
#endif

	/* lock debug start (if any) */
	checklock_start();
	log_init(0, 0, 0);

#ifdef SIGPIPE
	if(signal(SIGPIPE, &sigh) == SIG_ERR) {
		perror("could not install signal handler");
		return 1;
	}
#endif

	/* command line options */
	if(argc == 1) {
		usage(argv);
	}
	while( (c=getopt(argc, argv, "af:p:hnsud:")) != -1) {
		switch(c) {
			case 'f':
				svr = optarg;
				break;
			case 'p':
				pp2_client = optarg;
				pp_init(&sldns_write_uint16,
					&sldns_write_uint32);
				break;
			case 'a':
				onarrival = 1;
				break;
			case 'n':
				noanswer = 1;
				break;
			case 'u':
				udp = 1;
				break;
			case 's':
				usessl = 1;
				break;
			case 'd':
				if(atoi(optarg)==0 && strcmp(optarg,"0")!=0) {
					printf("error parsing delay, "
					    "number expected: %s\n", optarg);
					return 1;
				}
				delay = atoi(optarg);
				break;
			case 'h':
			case '?':
			default:
				usage(argv);
		}
	}
	argc -= optind;
	argv += optind;

	if(argc % 3 != 0) {
		printf("queries must be multiples of name,type,class\n");
		return 1;
	}
	if(usessl) {
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
	send_em(svr, pp2_client, udp, usessl, noanswer, onarrival, delay, argc, argv);
	checklock_stop();
#ifdef USE_WINSOCK
	WSACleanup();
#endif
	return 0;
}
