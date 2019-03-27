/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#define RIPVERSION RIPv2
#include <protocols/routed.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef sgi
#include <strings.h>
#include <bstring.h>
#endif

#define UNUSED __attribute__((unused))
#ifndef __RCSID
#define __RCSID(_s) static const char rcsid[] UNUSED = _s
#endif
#ifndef __COPYRIGHT
#define __COPYRIGHT(_s) static const char copyright[] UNUSED = _s
#endif
__COPYRIGHT("@(#) Copyright (c) 1983, 1988, 1993\n"
	    "The Regents of the University of California."
	    "  All rights reserved.\n");
#ifdef __NetBSD__
__RCSID("$NetBSD$");
#elif defined(__FreeBSD__)
__RCSID("$FreeBSD$");
#else
__RCSID("$Revision: 2.26 $");
#ident "$Revision: 2.26 $"
#endif

#ifndef sgi
#define _HAVE_SIN_LEN
#endif

#ifdef __NetBSD__
#include <md5.h>
#else
#define MD5_DIGEST_LEN 16
typedef struct {
	u_int32_t state[4];		/* state (ABCD) */
	u_int32_t count[2];		/* # of bits, modulo 2^64 (LSB 1st) */
	unsigned char buffer[64];	/* input buffer */
} MD5_CTX;
extern void MD5Init(MD5_CTX*);
extern void MD5Update(MD5_CTX*, u_char*, u_int);
extern void MD5Final(u_char[MD5_DIGEST_LEN], MD5_CTX*);
#endif


#define	WTIME	15		/* Time to wait for all responses */
#define	STIME	(250*1000)	/* usec to wait for another response */

int	soc;

const char *pgmname;

union {
	struct rip rip;
	char	packet[MAXPACKETSIZE+MAXPATHLEN];
} omsg_buf;
#define OMSG omsg_buf.rip
int omsg_len = sizeof(struct rip);

union {
	struct	rip rip;
	char	packet[MAXPACKETSIZE+1024];
	} imsg_buf;
#define IMSG imsg_buf.rip

int	nflag;				/* numbers, no names */
int	pflag;				/* play the `gated` game */
int	ripv2 = 1;			/* use RIP version 2 */
int	wtime = WTIME;
int	rflag;				/* 1=ask about a particular route */
int	trace, not_trace;		/* send trace command or not */
int	auth_type = RIP_AUTH_NONE;
char	passwd[RIP_AUTH_PW_LEN];
u_long	keyid;

struct timeval sent;			/* when query sent */

static char localhost_str[] = "localhost";
static char *default_argv[] = {localhost_str, 0};

static void rip_input(struct sockaddr_in*, int);
static int out(const char *);
static void trace_loop(char *argv[]) __attribute((__noreturn__));
static void query_loop(char *argv[], int) __attribute((__noreturn__));
static int getnet(char *, struct netinfo *);
static u_int std_mask(u_int);
static int parse_quote(char **, const char *, char *, char *, int);
static void usage(void);


int
main(int argc,
     char *argv[])
{
	int ch, bsize;
	char *p, *options, *value, delim;
	const char *result;

	OMSG.rip_nets[0].n_dst = RIP_DEFAULT;
	OMSG.rip_nets[0].n_family = RIP_AF_UNSPEC;
	OMSG.rip_nets[0].n_metric = htonl(HOPCNT_INFINITY);

	pgmname = argv[0];
	while ((ch = getopt(argc, argv, "np1w:r:t:a:")) != -1)
		switch (ch) {
		case 'n':
			not_trace = 1;
			nflag = 1;
			break;

		case 'p':
			not_trace = 1;
			pflag = 1;
			break;

		case '1':
			ripv2 = 0;
			break;

		case 'w':
			not_trace = 1;
			wtime = (int)strtoul(optarg, &p, 0);
			if (*p != '\0'
			    || wtime <= 0)
				usage();
			break;

		case 'r':
			not_trace = 1;
			if (rflag)
				usage();
			rflag = getnet(optarg, &OMSG.rip_nets[0]);
			if (!rflag) {
				struct hostent *hp = gethostbyname(optarg);
				if (hp == NULL) {
					fprintf(stderr, "%s: %s:",
						pgmname, optarg);
					herror(0);
					exit(1);
				}
				memcpy(&OMSG.rip_nets[0].n_dst, hp->h_addr,
				       sizeof(OMSG.rip_nets[0].n_dst));
				OMSG.rip_nets[0].n_family = RIP_AF_INET;
				OMSG.rip_nets[0].n_mask = -1;
				rflag = 1;
			}
			break;

		case 't':
			trace = 1;
			options = optarg;
			while (*options != '\0') {
				/* messy complications to make -W -Wall happy */
				static char on_str[] = "on";
				static char more_str[] = "more";
				static char off_str[] = "off";
				static char dump_str[] = "dump";
				static char *traceopts[] = {
#				    define TRACE_ON	0
					on_str,
#				    define TRACE_MORE	1
					more_str,
#				    define TRACE_OFF	2
					off_str,
#				    define TRACE_DUMP	3
					dump_str,
					0
				};
				result = "";
				switch (getsubopt(&options,traceopts,&value)) {
				case TRACE_ON:
					OMSG.rip_cmd = RIPCMD_TRACEON;
					if (!value
					    || strlen(value) > MAXPATHLEN)
					    usage();
					result = value;
					break;
				case TRACE_MORE:
					if (value)
					    usage();
					OMSG.rip_cmd = RIPCMD_TRACEON;
					break;
				case TRACE_OFF:
					if (value)
					    usage();
					OMSG.rip_cmd = RIPCMD_TRACEOFF;
					break;
				case TRACE_DUMP:
					if (value)
					    usage();
					OMSG.rip_cmd = RIPCMD_TRACEON;
					result = "dump/../table";
					break;
				default:
					usage();
				}
				strcpy((char*)OMSG.rip_tracefile, result);
				omsg_len += strlen(result) - sizeof(OMSG.ripun);
			}
			break;

		case 'a':
			not_trace = 1;
			p = strchr(optarg,'=');
			if (!p)
				usage();
			*p++ = '\0';
			if (!strcasecmp("passwd",optarg))
				auth_type = RIP_AUTH_PW;
			else if (!strcasecmp("md5_passwd",optarg))
				auth_type = RIP_AUTH_MD5;
			else
				usage();
			if (0 > parse_quote(&p,"|",&delim,
					    passwd, sizeof(passwd)))
				usage();
			if (auth_type == RIP_AUTH_MD5
			    && delim == '|') {
				keyid = strtoul(p+1,&p,0);
				if (keyid > 255 || *p != '\0')
					usage();
			} else if (delim != '\0') {
				usage();
			}
			break;

		default:
			usage();
	}
	argv += optind;
	argc -= optind;
	if (not_trace && trace)
		usage();
	if (argc == 0) {
		argc = 1;
		argv = default_argv;
	}

	soc = socket(AF_INET, SOCK_DGRAM, 0);
	if (soc < 0) {
		perror("socket");
		exit(2);
	}

	/* be prepared to receive a lot of routes */
	for (bsize = 127*1024; ; bsize -= 1024) {
		if (setsockopt(soc, SOL_SOCKET, SO_RCVBUF,
			       &bsize, sizeof(bsize)) == 0)
			break;
		if (bsize <= 4*1024) {
			perror("setsockopt SO_RCVBUF");
			break;
		}
	}

	if (trace)
		trace_loop(argv);
	else
		query_loop(argv, argc);
	/* NOTREACHED */
	return 0;
}


static void
usage(void)
{
	fprintf(stderr,
		"usage:  rtquery [-np1] [-r tgt_rt] [-w wtime]"
		" [-a type=passwd] host1 [host2 ...]\n"
		"\trtquery -t {on=filename|more|off|dump}"
				" host1 [host2 ...]\n");
	exit(1);
}


/* tell the target hosts about tracing
 */
static void
trace_loop(char *argv[])
{
	struct sockaddr_in myaddr;
	int res;

	if (geteuid() != 0) {
		(void)fprintf(stderr, "-t requires UID 0\n");
		exit(1);
	}

	if (ripv2) {
		OMSG.rip_vers = RIPv2;
	} else {
		OMSG.rip_vers = RIPv1;
	}

	memset(&myaddr, 0, sizeof(myaddr));
	myaddr.sin_family = AF_INET;
#ifdef _HAVE_SIN_LEN
	myaddr.sin_len = sizeof(myaddr);
#endif
	myaddr.sin_port = htons(IPPORT_RESERVED-1);
	while (bind(soc, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
		if (errno != EADDRINUSE
		    || myaddr.sin_port == 0) {
			perror("bind");
			exit(2);
		}
		myaddr.sin_port = htons(ntohs(myaddr.sin_port)-1);
	}

	res = 1;
	while (*argv != NULL) {
		if (out(*argv++) <= 0)
			res = 0;
	}
	exit(res);
}


/* query all of the listed hosts
 */
static void
query_loop(char *argv[], int argc)
{
#	define NA0 (OMSG.rip_auths[0])
#	define NA2 (OMSG.rip_auths[2])
	struct seen {
		struct seen *next;
		struct in_addr addr;
	} *seen, *sp;
	int answered = 0;
	int cc;
	fd_set bits;
	struct timeval now, delay;
	struct sockaddr_in from;
	int fromlen;
	MD5_CTX md5_ctx;


	OMSG.rip_cmd = (pflag) ? RIPCMD_POLL : RIPCMD_REQUEST;
	if (ripv2) {
		OMSG.rip_vers = RIPv2;
		if (auth_type == RIP_AUTH_PW) {
			OMSG.rip_nets[1] = OMSG.rip_nets[0];
			NA0.a_family = RIP_AF_AUTH;
			NA0.a_type = RIP_AUTH_PW;
			memcpy(NA0.au.au_pw, passwd, RIP_AUTH_PW_LEN);
			omsg_len += sizeof(OMSG.rip_nets[0]);

		} else if (auth_type == RIP_AUTH_MD5) {
			OMSG.rip_nets[1] = OMSG.rip_nets[0];
			NA0.a_family = RIP_AF_AUTH;
			NA0.a_type = RIP_AUTH_MD5;
			NA0.au.a_md5.md5_keyid = (int8_t)keyid;
			NA0.au.a_md5.md5_auth_len = RIP_AUTH_MD5_KEY_LEN;
			NA0.au.a_md5.md5_seqno = 0;
			cc = (char *)&NA2-(char *)&OMSG;
			NA0.au.a_md5.md5_pkt_len = htons(cc);
			NA2.a_family = RIP_AF_AUTH;
			NA2.a_type = htons(1);
			MD5Init(&md5_ctx);
			MD5Update(&md5_ctx,
				  (u_char *)&OMSG, cc);
			MD5Update(&md5_ctx,
				  (u_char *)passwd, RIP_AUTH_MD5_HASH_LEN);
			MD5Final(NA2.au.au_pw, &md5_ctx);
			omsg_len += 2*sizeof(OMSG.rip_nets[0]);
		}

	} else {
		OMSG.rip_vers = RIPv1;
		OMSG.rip_nets[0].n_mask = 0;
	}

	/* ask the first (valid) host */
	seen = NULL;
	while (0 > out(*argv++)) {
		if (*argv == NULL)
			exit(1);
		answered++;
	}

	FD_ZERO(&bits);
	for (;;) {
		FD_SET(soc, &bits);
		delay.tv_sec = 0;
		delay.tv_usec = STIME;
		cc = select(soc+1, &bits, 0,0, &delay);
		if (cc > 0) {
			fromlen = sizeof(from);
			cc = recvfrom(soc, imsg_buf.packet,
				      sizeof(imsg_buf.packet), 0,
				      (struct sockaddr *)&from, &fromlen);
			if (cc < 0) {
				perror("recvfrom");
				exit(1);
			}
			/* count the distinct responding hosts.
			 * You cannot match responding hosts with
			 * addresses to which queries were transmitted,
			 * because a router might respond with a
			 * different source address.
			 */
			for (sp = seen; sp != NULL; sp = sp->next) {
				if (sp->addr.s_addr == from.sin_addr.s_addr)
					break;
			}
			if (sp == NULL) {
				sp = malloc(sizeof(*sp));
				if (sp == NULL) {
					fprintf(stderr,
						"rtquery: malloc failed\n");
					exit(1);
				}
				sp->addr = from.sin_addr;
				sp->next = seen;
				seen = sp;
				answered++;
			}

			rip_input(&from, cc);
			continue;
		}

		if (cc < 0) {
			if (errno == EINTR)
				continue;
			perror("select");
			exit(1);
		}

		/* After a pause in responses, probe another host.
		 * This reduces the intermingling of answers.
		 */
		while (*argv != NULL && out(*argv++) < 0)
			answered++;

		/* continue until no more packets arrive
		 * or we have heard from all hosts
		 */
		if (answered >= argc)
			break;

		/* or until we have waited a long time
		 */
		if (gettimeofday(&now, 0) < 0) {
			perror("gettimeofday(now)");
			exit(1);
		}
		if (sent.tv_sec + wtime <= now.tv_sec)
			break;
	}

	/* fail if there was no answer */
	exit (answered >= argc ? 0 : 1);
}


/* send to one host
 */
static int
out(const char *host)
{
	struct sockaddr_in router;
	struct hostent *hp;

	if (gettimeofday(&sent, 0) < 0) {
		perror("gettimeofday(sent)");
		return -1;
	}

	memset(&router, 0, sizeof(router));
	router.sin_family = AF_INET;
#ifdef _HAVE_SIN_LEN
	router.sin_len = sizeof(router);
#endif
	if (!inet_aton(host, &router.sin_addr)) {
		hp = gethostbyname(host);
		if (hp == NULL) {
			herror(host);
			return -1;
		}
		memcpy(&router.sin_addr, hp->h_addr, sizeof(router.sin_addr));
	}
	router.sin_port = htons(RIP_PORT);

	if (sendto(soc, &omsg_buf, omsg_len, 0,
		   (struct sockaddr *)&router, sizeof(router)) < 0) {
		perror(host);
		return -1;
	}

	return 0;
}


/*
 * Convert string to printable characters
 */
static char *
qstring(u_char *s, int len)
{
	static char buf[8*20+1];
	char *p;
	u_char *s2, c;


	for (p = buf; len != 0 && p < &buf[sizeof(buf)-1]; len--) {
		c = *s++;
		if (c == '\0') {
			for (s2 = s+1; s2 < &s[len]; s2++) {
				if (*s2 != '\0')
					break;
			}
			if (s2 >= &s[len])
			    goto exit;
		}

		if (c >= ' ' && c < 0x7f && c != '\\') {
			*p++ = c;
			continue;
		}
		*p++ = '\\';
		switch (c) {
		case '\\':
			*p++ = '\\';
			break;
		case '\n':
			*p++= 'n';
			break;
		case '\r':
			*p++= 'r';
			break;
		case '\t':
			*p++ = 't';
			break;
		case '\b':
			*p++ = 'b';
			break;
		default:
			p += sprintf(p,"%o",c);
			break;
		}
	}
exit:
	*p = '\0';
	return buf;
}


/*
 * Handle an incoming RIP packet.
 */
static void
rip_input(struct sockaddr_in *from,
	  int size)
{
	struct netinfo *n, *lim;
	struct in_addr in;
	const char *name;
	char net_buf[80];
	u_char hash[RIP_AUTH_MD5_KEY_LEN];
	MD5_CTX md5_ctx;
	u_char md5_authed = 0;
	u_int mask, dmask;
	char *sp;
	int i;
	struct hostent *hp;
	struct netent *np;
	struct netauth *na;


	if (nflag) {
		printf("%s:", inet_ntoa(from->sin_addr));
	} else {
		hp = gethostbyaddr((char*)&from->sin_addr,
				   sizeof(struct in_addr), AF_INET);
		if (hp == NULL) {
			printf("%s:",
			       inet_ntoa(from->sin_addr));
		} else {
			printf("%s (%s):", hp->h_name,
			       inet_ntoa(from->sin_addr));
		}
	}
	if (IMSG.rip_cmd != RIPCMD_RESPONSE) {
		printf("\n    unexpected response type %d\n", IMSG.rip_cmd);
		return;
	}
	printf(" RIPv%d%s %d bytes\n", IMSG.rip_vers,
	       (IMSG.rip_vers != RIPv1 && IMSG.rip_vers != RIPv2) ? " ?" : "",
	       size);
	if (size > MAXPACKETSIZE) {
		if (size > (int)sizeof(imsg_buf) - (int)sizeof(*n)) {
			printf("       at least %d bytes too long\n",
			       size-MAXPACKETSIZE);
			size = (int)sizeof(imsg_buf) - (int)sizeof(*n);
		} else {
			printf("       %d bytes too long\n",
			       size-MAXPACKETSIZE);
		}
	} else if (size%sizeof(*n) != sizeof(struct rip)%sizeof(*n)) {
		printf("    response of bad length=%d\n", size);
	}

	n = IMSG.rip_nets;
	lim = (struct netinfo *)((char*)n + size) - 1;
	for (; n <= lim; n++) {
		name = "";
		if (n->n_family == RIP_AF_INET) {
			in.s_addr = n->n_dst;
			(void)strcpy(net_buf, inet_ntoa(in));

			mask = ntohl(n->n_mask);
			dmask = mask & -mask;
			if (mask != 0) {
				sp = &net_buf[strlen(net_buf)];
				if (IMSG.rip_vers == RIPv1) {
					(void)sprintf(sp," mask=%#x ? ",mask);
					mask = 0;
				} else if (mask + dmask == 0) {
					for (i = 0;
					     (i != 32
					      && ((1<<i)&mask) == 0);
					     i++)
						continue;
					(void)sprintf(sp, "/%d",32-i);
				} else {
					(void)sprintf(sp," (mask %#x)", mask);
				}
			}

			if (!nflag) {
				if (mask == 0) {
					mask = std_mask(in.s_addr);
					if ((ntohl(in.s_addr) & ~mask) != 0)
						mask = 0;
				}
				/* Without a netmask, do not worry about
				 * whether the destination is a host or a
				 * network. Try both and use the first name
				 * we get.
				 *
				 * If we have a netmask we can make a
				 * good guess.
				 */
				if ((in.s_addr & ~mask) == 0) {
					np = getnetbyaddr((long)in.s_addr,
							  AF_INET);
					if (np != NULL)
						name = np->n_name;
					else if (in.s_addr == 0)
						name = "default";
				}
				if (name[0] == '\0'
				    && ((in.s_addr & ~mask) != 0
					|| mask == 0xffffffff)) {
					hp = gethostbyaddr((char*)&in,
							   sizeof(in),
							   AF_INET);
					if (hp != NULL)
						name = hp->h_name;
				}
			}

		} else if (n->n_family == RIP_AF_AUTH) {
			na = (struct netauth*)n;
			if (na->a_type == RIP_AUTH_PW
			    && n == IMSG.rip_nets) {
				(void)printf("  Password Authentication:"
					     " \"%s\"\n",
					     qstring(na->au.au_pw,
						     RIP_AUTH_PW_LEN));
				continue;
			}

			if (na->a_type == RIP_AUTH_MD5
			    && n == IMSG.rip_nets) {
				(void)printf("  MD5 Auth"
					     " len=%d KeyID=%d"
					     " auth_len=%d"
					     " seqno=%#x"
					     " rsvd=%#x,%#x\n",
					     ntohs(na->au.a_md5.md5_pkt_len),
					     na->au.a_md5.md5_keyid,
					     na->au.a_md5.md5_auth_len,
					     (int)ntohl(na->au.a_md5.md5_seqno),
					     na->au.a_md5.rsvd[0],
					     na->au.a_md5.rsvd[1]);
				md5_authed = 1;
				continue;
			}
			(void)printf("  Authentication type %d: ",
				     ntohs(na->a_type));
			for (i = 0; i < (int)sizeof(na->au.au_pw); i++)
				(void)printf("%02x ", na->au.au_pw[i]);
			putc('\n', stdout);
			if (md5_authed && n+1 > lim
			    && na->a_type == ntohs(1)) {
				MD5Init(&md5_ctx);
				MD5Update(&md5_ctx, (u_char *)&IMSG,
					  (char *)na-(char *)&IMSG
					  +RIP_AUTH_MD5_HASH_XTRA);
				MD5Update(&md5_ctx, (u_char *)passwd,
					  RIP_AUTH_MD5_KEY_LEN);
				MD5Final(hash, &md5_ctx);
				(void)printf("    %s hash\n",
					     memcmp(hash, na->au.au_pw,
						    sizeof(hash))
					     ? "WRONG" : "correct");
			}
			continue;

		} else {
			(void)sprintf(net_buf, "(af %#x) %d.%d.%d.%d",
				      ntohs(n->n_family),
				      (u_char)(n->n_dst >> 24),
				      (u_char)(n->n_dst >> 16),
				      (u_char)(n->n_dst >> 8),
				      (u_char)n->n_dst);
		}

		(void)printf("  %-18s metric %2d %-10s",
			     net_buf, (int)ntohl(n->n_metric), name);

		if (n->n_nhop != 0) {
			in.s_addr = n->n_nhop;
			if (nflag)
				hp = NULL;
			else
				hp = gethostbyaddr((char*)&in, sizeof(in),
						   AF_INET);
			(void)printf(" nhop=%-15s%s",
				     (hp != NULL) ? hp->h_name : inet_ntoa(in),
				     (IMSG.rip_vers == RIPv1) ? " ?" : "");
		}
		if (n->n_tag != 0)
			(void)printf(" tag=%#x%s", n->n_tag,
				     (IMSG.rip_vers == RIPv1) ? " ?" : "");
		putc('\n', stdout);
	}
}


/* Return the classical netmask for an IP address.
 */
static u_int
std_mask(u_int addr)			/* in network order */
{
	addr = ntohl(addr);		/* was a host, not a network */

	if (addr == 0)			/* default route has mask 0 */
		return 0;
	if (IN_CLASSA(addr))
		return IN_CLASSA_NET;
	if (IN_CLASSB(addr))
		return IN_CLASSB_NET;
	return IN_CLASSC_NET;
}


/* get a network number as a name or a number, with an optional "/xx"
 * netmask.
 */
static int				/* 0=bad */
getnet(char *name,
       struct netinfo *rt)
{
	int i;
	struct netent *nentp;
	u_int mask;
	struct in_addr in;
	char hname[MAXHOSTNAMELEN+1];
	char *mname, *p;


	/* Detect and separate "1.2.3.4/24"
	 */
	if (NULL != (mname = strrchr(name,'/'))) {
		i = (int)(mname - name);
		if (i > (int)sizeof(hname)-1)	/* name too long */
			return 0;
		memmove(hname, name, i);
		hname[i] = '\0';
		mname++;
		name = hname;
	}

	nentp = getnetbyname(name);
	if (nentp != NULL) {
		in.s_addr = nentp->n_net;
	} else if (inet_aton(name, &in) == 1) {
		in.s_addr = ntohl(in.s_addr);
	} else {
		return 0;
	}

	if (mname == NULL) {
		mask = std_mask(in.s_addr);
		if ((~mask & in.s_addr) != 0)
			mask = 0xffffffff;
	} else {
		mask = (u_int)strtoul(mname, &p, 0);
		if (*p != '\0' || mask > 32)
			return 0;
		mask = 0xffffffff << (32-mask);
	}

	rt->n_dst = htonl(in.s_addr);
	rt->n_family = RIP_AF_INET;
	rt->n_mask = htonl(mask);
	return 1;
}


/* strtok(), but honoring backslash
 */
static int				/* -1=bad */
parse_quote(char **linep,
	    const char *delims,
	    char *delimp,
	    char *buf,
	    int	lim)
{
	char c, *pc;
	const char *p;


	pc = *linep;
	if (*pc == '\0')
		return -1;

	for (;;) {
		if (lim == 0)
			return -1;
		c = *pc++;
		if (c == '\0')
			break;

		if (c == '\\' && *pc != '\0') {
			if ((c = *pc++) == 'n') {
				c = '\n';
			} else if (c == 'r') {
				c = '\r';
			} else if (c == 't') {
				c = '\t';
			} else if (c == 'b') {
				c = '\b';
			} else if (c >= '0' && c <= '7') {
				c -= '0';
				if (*pc >= '0' && *pc <= '7') {
					c = (c<<3)+(*pc++ - '0');
					if (*pc >= '0' && *pc <= '7')
					    c = (c<<3)+(*pc++ - '0');
				}
			}

		} else {
			for (p = delims; *p != '\0'; ++p) {
				if (*p == c)
					goto exit;
			}
		}

		*buf++ = c;
		--lim;
	}
exit:
	if (delimp != NULL)
		*delimp = c;
	*linep = pc-1;
	if (lim != 0)
		*buf = '\0';
	return 0;
}
