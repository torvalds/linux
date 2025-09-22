/*	$OpenBSD: raddauth.c,v 1.33 2024/07/18 02:45:31 yasuoka Exp $	*/

/*-
 * Copyright (c) 1996, 1997 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Berkeley Software Design,
 *      Inc.
 * 4. The name of Berkeley Software Design, Inc.  may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN, INC. BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	BSDI $From: raddauth.c,v 1.6 1998/04/14 00:39:04 prb Exp $
 */
/*
 * Copyright(c) 1996 by tfm associates.
 * All rights reserved.
 *
 * tfm associates
 * P.O. Box 2086
 * Eugene OR 97402-0031
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of tfm associates may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TFM ASSOC``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TFM ASSOCIATES BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include <login_cap.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <readpassphrase.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include "login_radius.h"


#define	MAXPWNETNAM		64	/* longest username */
#define MAXSECRETLEN		128	/* maximum length of secret */

#define AUTH_VECTOR_LEN			16
#define AUTH_HDR_LEN			20
#define	AUTH_PASS_LEN			(256 - 16)
#define AUTH_MSGAUTH_LEN		16
#define	PW_AUTHENTICATION_REQUEST	1
#define	PW_AUTHENTICATION_ACK		2
#define	PW_AUTHENTICATION_REJECT	3
#define PW_ACCESS_CHALLENGE		11
#define	PW_USER_NAME			1
#define	PW_PASSWORD			2
#define	PW_CLIENT_ID			4
#define	PW_CLIENT_PORT_ID		5
#define PW_PORT_MESSAGE			18
#define PW_STATE			24
#define PW_MSG_AUTH			80

#ifndef	RADIUS_DIR
#define RADIUS_DIR		"/etc/raddb"
#endif
#define RADIUS_SERVERS		"servers"

char *radius_dir = RADIUS_DIR;
char auth_secret[MAXSECRETLEN+1];
volatile sig_atomic_t timedout;
int alt_retries;
int retries;
int sockfd;
int timeout;
in_addr_t alt_server;
in_addr_t auth_server;
in_port_t radius_port;

typedef struct {
	u_char	code;
	u_char	id;
	u_short	length;
	u_char	vector[AUTH_VECTOR_LEN];
	u_char	data[4096 - AUTH_HDR_LEN];
} auth_hdr_t;

void servtimeout(int);
in_addr_t get_ipaddr(char *);
in_addr_t gethost(void);
int rad_recv(char *, char *, u_char *);
void parse_challenge(auth_hdr_t *, char *, char *);
void rad_request(u_char, char *, char *, int, char *, char *);
void getsecret(void);

/*
 * challenge -- NULL for interactive service
 * password -- NULL for interactive service and when requesting a challenge
 */
int
raddauth(char *username, char *class, char *style, char *challenge,
    char *password, char **emsg)
{
	static char _pwstate[1024];
	u_char req_id;
	char *userstyle, *passwd, *pwstate, *rad_service;
	char pbuf[AUTH_PASS_LEN+1];
	int auth_port;
	char vector[AUTH_VECTOR_LEN+1], *p, *v;
	int i;
	login_cap_t *lc;
	u_int32_t r;
	struct servent *svp;
	struct sockaddr_in sin;
	struct sigaction sa;
	const char *errstr;

	memset(_pwstate, 0, sizeof(_pwstate));
	pwstate = password ? challenge : _pwstate;

	if ((lc = login_getclass(class)) == NULL) {
		snprintf(_pwstate, sizeof(_pwstate),
		    "%s: no such class", class);
		*emsg = _pwstate;
		return (1);
	}

	rad_service = login_getcapstr(lc, "radius-port", "radius", "radius");
	timeout = login_getcapnum(lc, "radius-timeout", 2, 2);
	retries = login_getcapnum(lc, "radius-retries", 6, 6);

	if (timeout < 1)
		timeout = 1;
	if (retries < 2)
		retries = 2;

	if (challenge == NULL) {
		passwd = NULL;
		v = login_getcapstr(lc, "radius-challenge-styles",
		    NULL, NULL);
		i = strlen(style);
		while (v && (p = strstr(v, style)) != NULL) {
			if ((p == v || p[-1] == ',') &&
			    (p[i] == ',' || p[i] == '\0')) {
				passwd = "";
				break;
			}
			v = p+1;
		}
		if (passwd == NULL)
			passwd = readpassphrase("Password:", pbuf, sizeof(pbuf),
			    RPP_ECHO_OFF);
	} else
		passwd = password;
	if (passwd == NULL)
		passwd = "";

	if ((v = login_getcapstr(lc, "radius-server", NULL, NULL)) == NULL){
		*emsg = "radius-server not configured";
		return (1);
	}

	auth_server = get_ipaddr(v);

	if ((v = login_getcapstr(lc, "radius-server-alt", NULL, NULL)) == NULL)
		alt_server = 0;
	else {
		alt_server = get_ipaddr(v);
		alt_retries = retries/2;
		retries >>= 1;
	}

	/* get port number */
	radius_port = strtonum(rad_service, 1, UINT16_MAX, &errstr);
	if (errstr) {
		svp = getservbyname(rad_service, "udp");
		if (svp == NULL) {
			snprintf(_pwstate, sizeof(_pwstate),
			    "No such service: %s/udp", rad_service);
			*emsg = _pwstate;
			return (1);
		}
		radius_port = svp->s_port;
	} else
		radius_port = htons(radius_port);

	/* get the secret from the servers file */
	getsecret();

	/* set up socket */
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		snprintf(_pwstate, sizeof(_pwstate), "%s", strerror(errno));
		*emsg = _pwstate;
		return (1);
	}

	/* set up client structure */
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = radius_port;

	req_id = (u_char) arc4random();
	auth_port = ttyslot();
	if (auth_port == 0)
		auth_port = (int)getppid();
	if (strcmp(style, "radius") != 0) {
		if (asprintf(&userstyle, "%s:%s", username, style) == -1)
			err(1, NULL);
	} else
		userstyle = username;

	/* generate random vector */
	for (i = 0; i < AUTH_VECTOR_LEN;) {
		r = arc4random();
		memcpy(&vector[i], &r, sizeof(r));
		i += sizeof(r);
	}
	vector[AUTH_VECTOR_LEN] = '\0';

	sigemptyset(&sa.sa_mask);
	sa.sa_handler = servtimeout;
	sa.sa_flags = 0;		/* don't restart system calls */
	(void)sigaction(SIGALRM, &sa, NULL);
retry:
	if (timedout) {
		timedout = 0;
		if (--retries <= 0) {
			/*
			 * If we ran out of tries but there is an alternate
			 * server, switch to it and try again.
			 */
			if (alt_retries) {
				auth_server = alt_server;
				retries = alt_retries;
				alt_retries = 0;
				getsecret();
			} else
				warnx("no response from authentication server");
		}
	}

	if (retries > 0) {
		rad_request(req_id, userstyle, passwd, auth_port, vector,
		    pwstate);

		switch (i = rad_recv(_pwstate, challenge, vector)) {
		case PW_AUTHENTICATION_ACK:
			/*
			 * Make sure we don't think a challenge was issued.
			 */
			if (challenge)
				*challenge = '\0';
			return (0);

		case PW_AUTHENTICATION_REJECT:
			return (1);

		case PW_ACCESS_CHALLENGE:
			/*
			 * If this is a response then reject them if
			 * we got a challenge.
			 */
			if (password)
				return (1);
			/*
			 * If we wanted a challenge, just return
			 */
			if (challenge) {
				if (strcmp(challenge, _pwstate) != 0)
					syslog(LOG_WARNING,
				    "challenge for %s does not match state",
				    userstyle);
				return (0);
			}
			req_id++;
			if ((passwd = readpassphrase("", pbuf, sizeof(pbuf),
				    RPP_ECHO_OFF)) == NULL)
				passwd = "";
			break;

		default:
			if (timedout)
				goto retry;
			snprintf(_pwstate, sizeof(_pwstate),
			    "invalid response type %d\n", i);
			*emsg = _pwstate;
			return (1);
		}
	}
	return (1);
}

/*
 * Build a radius authentication digest and submit it to the radius server
 */
void
rad_request(u_char id, char *name, char *password, int port, char *vector,
    char *state)
{
	auth_hdr_t auth;
	int i, len, secretlen, total_length, p;
	struct sockaddr_in sin;
	u_char md5buf[MAXSECRETLEN+AUTH_VECTOR_LEN], digest[AUTH_VECTOR_LEN],
	    pass_buf[AUTH_PASS_LEN], *pw, *ptr, *ma;
	u_int length;
	in_addr_t ipaddr;
	MD5_CTX context;

	memset(&auth, 0, sizeof(auth));
	auth.code = PW_AUTHENTICATION_REQUEST;
	auth.id = id;
	memcpy(auth.vector, vector, AUTH_VECTOR_LEN);
	total_length = AUTH_HDR_LEN;
	ptr = auth.data;

	/* Preserve space for msgauth */
	*ptr++ = PW_MSG_AUTH;
	length = 16;
	*ptr++ = length + 2;
	ma = ptr;
	memset(ma, 0, 16);
	ptr += length;
	total_length += length + 2;

	/* User name */
	*ptr++ = PW_USER_NAME;
	length = strlen(name);
	if (length > MAXPWNETNAM)
		length = MAXPWNETNAM;
	*ptr++ = length + 2;
	memcpy(ptr, name, length);
	ptr += length;
	total_length += length + 2;

	/* Password */
	length = strlen(password);
	if (length > AUTH_PASS_LEN)
		length = AUTH_PASS_LEN;

	p = (length + AUTH_VECTOR_LEN - 1) / AUTH_VECTOR_LEN;
	*ptr++ = PW_PASSWORD;
	*ptr++ = p * AUTH_VECTOR_LEN + 2;

	memset(pass_buf, 0, sizeof(pass_buf));		/* must zero fill */
	strlcpy((char *)pass_buf, password, sizeof(pass_buf));

	/* Calculate the md5 digest */
	secretlen = strlen(auth_secret);
	memcpy(md5buf, auth_secret, secretlen);
	memcpy(md5buf + secretlen, auth.vector, AUTH_VECTOR_LEN);

	total_length += 2;

	/* XOR the password into the md5 digest */
	pw = pass_buf;
	while (p-- > 0) {
		MD5_Init(&context);
		MD5_Update(&context, md5buf, secretlen + AUTH_VECTOR_LEN);
		MD5_Final(digest, &context);
		for (i = 0; i < AUTH_VECTOR_LEN; ++i) {
			*ptr = digest[i] ^ *pw;
			md5buf[secretlen+i] = *ptr++;
			*pw++ = '\0';
		}
		total_length += AUTH_VECTOR_LEN;
	}
	explicit_bzero(pass_buf, strlen(pass_buf));

	/* Client id */
	*ptr++ = PW_CLIENT_ID;
	*ptr++ = sizeof(in_addr_t) + 2;
	ipaddr = gethost();
	memcpy(ptr, &ipaddr, sizeof(in_addr_t));
	ptr += sizeof(in_addr_t);
	total_length += sizeof(in_addr_t) + 2;

	/* client port */
	*ptr++ = PW_CLIENT_PORT_ID;
	*ptr++ = sizeof(in_addr_t) + 2;
	port = htonl(port);
	memcpy(ptr, &port, sizeof(int));
	ptr += sizeof(int);
	total_length += sizeof(int) + 2;

	/* Append the state info */
	if ((state != NULL) && (strlen(state) > 0)) {
		len = strlen(state);
		*ptr++ = PW_STATE;
		*ptr++ = len + 2;
		memcpy(ptr, state, len);
		ptr += len;
		total_length += len + 2;
	}

	auth.length = htons(total_length);

	/* Calc msgauth */
	if (HMAC(EVP_md5(), auth_secret, secretlen, (unsigned char *)&auth,
	    total_length, ma, NULL) == NULL)
		errx(1, "HMAC() failed");

	memset(&sin, 0, sizeof (sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = auth_server;
	sin.sin_port = radius_port;
	if (sendto(sockfd, &auth, total_length, 0, (struct sockaddr *)&sin,
	    sizeof(sin)) == -1)
		err(1, NULL);
}

/*
 * Receive UDP responses from the radius server
 */
int
rad_recv(char *state, char *challenge, u_char *req_vector)
{
	auth_hdr_t auth;
	socklen_t salen;
	struct sockaddr_in sin;
	u_char recv_vector[AUTH_VECTOR_LEN], test_vector[AUTH_VECTOR_LEN];
	MD5_CTX context;
	ssize_t total_length;

	salen = sizeof(sin);

	alarm(timeout);
	total_length = recvfrom(sockfd, &auth, sizeof(auth), 0,
	    (struct sockaddr *)&sin, &salen);
	alarm(0);
	if (total_length < AUTH_HDR_LEN) {
		if (timedout)
			return(-1);
		errx(1, "bogus auth packet from server");
	}
	if (ntohs(auth.length) > total_length)
		errx(1, "bogus auth packet from server");

	if (sin.sin_addr.s_addr != auth_server)
		errx(1, "bogus authentication server");

	/* verify server's shared secret */
	memcpy(recv_vector, auth.vector, AUTH_VECTOR_LEN);
	memcpy(auth.vector, req_vector, AUTH_VECTOR_LEN);
	MD5_Init(&context);
	MD5_Update(&context, (u_char *)&auth, ntohs(auth.length));
	MD5_Update(&context, auth_secret, strlen(auth_secret));
	MD5_Final(test_vector, &context);
	if (memcmp(recv_vector, test_vector, AUTH_VECTOR_LEN) != 0)
		errx(1, "shared secret incorrect");

	if (auth.code == PW_ACCESS_CHALLENGE)
		parse_challenge(&auth, state, challenge);

	return (auth.code);
}

/*
 * Get IP address of local hostname
 */
in_addr_t
gethost(void)
{
	char hostname[HOST_NAME_MAX+1];

	if (gethostname(hostname, sizeof(hostname)))
		err(1, "gethost");
	return (get_ipaddr(hostname));
}

/*
 * Get an IP address in host in_addr_t notation from a hostname or dotted quad.
 */
in_addr_t
get_ipaddr(char *host)
{
	struct hostent *hp;

	if ((hp = gethostbyname(host)) == NULL)
		return (0);

	return (((struct in_addr *)hp->h_addr)->s_addr);
}

/*
 * Get the secret from the servers file
 */
void
getsecret(void)
{
	FILE *servfd;
	char *host, *secret, buffer[PATH_MAX];
	size_t len;

	snprintf(buffer, sizeof(buffer), "%s/%s",
	    radius_dir, RADIUS_SERVERS);

	if ((servfd = fopen(buffer, "r")) == NULL) {
		syslog(LOG_ERR, "%s: %m", buffer);
		return;
	}

	secret = NULL;			/* Keeps gcc happy */
	while ((host = fgetln(servfd, &len)) != NULL) {
		if (*host == '#') {
			memset(host, 0, len);
			continue;
		}
		if (host[len-1] == '\n')
			--len;
		else {
			/* No trailing newline, must allocate len+1 for NUL */
			if ((secret = malloc(len + 1)) == NULL) {
				memset(host, 0, len);
				continue;
			}
			memcpy(secret, host, len);
			memset(host, 0, len);
			host = secret;
		}
		while (len > 0 && isspace((unsigned char)host[--len]))
			;
		host[++len] = '\0';
		while (isspace((unsigned char)*host)) {
			++host;
			--len;
		}
		if (*host == '\0')
			continue;
		secret = host;
		while (*secret && !isspace((unsigned char)*secret))
			++secret;
		if (*secret)
			*secret++ = '\0';
		if (get_ipaddr(host) != auth_server) {
			memset(host, 0, len);
			continue;
		}
		while (isspace((unsigned char)*secret))
			++secret;
		if (*secret)
			break;
	}
	if (host) {
		strlcpy(auth_secret, secret, sizeof(auth_secret));
		memset(host, 0, len);
	}
	fclose(servfd);
}

void
servtimeout(int signo)
{

	timedout = 1;
}

/*
 * Parse a challenge received from the server
 */
void
parse_challenge(auth_hdr_t *authhdr, char *state, char *challenge)
{
	int length;
	int attribute, attribute_len;
	u_char *ptr;

	ptr = authhdr->data;
	length = ntohs(authhdr->length) - AUTH_HDR_LEN;

	*state = 0;

	while (length > 0) {
		attribute = *ptr++;
		attribute_len = *ptr++;
		length -= attribute_len;
		attribute_len -= 2;

		switch (attribute) {
		case PW_PORT_MESSAGE:
			if (challenge) {
				memcpy(challenge, ptr, attribute_len);
				challenge[attribute_len] = '\0';
			} else
				printf("%.*s", attribute_len, ptr);
			break;
		case PW_STATE:
			memcpy(state, ptr, attribute_len);
			state[attribute_len] = '\0';
			break;
		}
		ptr += attribute_len;
	}
}
