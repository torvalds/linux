/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 1998 Juniper Networks, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef WITH_SSL
#include <openssl/hmac.h>
#include <openssl/md5.h>
#define MD5Init MD5_Init
#define MD5Update MD5_Update
#define MD5Final MD5_Final
#else
#define MD5_DIGEST_LENGTH 16
#include <md5.h>
#endif

#define	MAX_FIELDS	7

/* We need the MPPE_KEY_LEN define */
#include <netgraph/ng_mppc.h>

#include <errno.h>
#include <netdb.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "radlib_private.h"

static void	 clear_password(struct rad_handle *);
static void	 generr(struct rad_handle *, const char *, ...)
		    __printflike(2, 3);
static void	 insert_scrambled_password(struct rad_handle *, int);
static void	 insert_request_authenticator(struct rad_handle *, int);
static void	 insert_message_authenticator(struct rad_handle *, int);
static int	 is_valid_response(struct rad_handle *, int,
		    const struct sockaddr_in *);
static int	 put_password_attr(struct rad_handle *, int,
		    const void *, size_t);
static int	 put_raw_attr(struct rad_handle *, int,
		    const void *, size_t);
static int	 split(char *, char *[], int, char *, size_t);

static void
clear_password(struct rad_handle *h)
{
	if (h->pass_len != 0) {
		memset(h->pass, 0, h->pass_len);
		h->pass_len = 0;
	}
	h->pass_pos = 0;
}

static void
generr(struct rad_handle *h, const char *format, ...)
{
	va_list		 ap;

	va_start(ap, format);
	vsnprintf(h->errmsg, ERRSIZE, format, ap);
	va_end(ap);
}

static void
insert_scrambled_password(struct rad_handle *h, int srv)
{
	MD5_CTX ctx;
	unsigned char md5[MD5_DIGEST_LENGTH];
	const struct rad_server *srvp;
	int padded_len;
	int pos;

	srvp = &h->servers[srv];
	padded_len = h->pass_len == 0 ? 16 : (h->pass_len+15) & ~0xf;

	memcpy(md5, &h->out[POS_AUTH], LEN_AUTH);
	for (pos = 0;  pos < padded_len;  pos += 16) {
		int i;

		/* Calculate the new scrambler */
		MD5Init(&ctx);
		MD5Update(&ctx, srvp->secret, strlen(srvp->secret));
		MD5Update(&ctx, md5, 16);
		MD5Final(md5, &ctx);

		/*
		 * Mix in the current chunk of the password, and copy
		 * the result into the right place in the request.  Also
		 * modify the scrambler in place, since we will use this
		 * in calculating the scrambler for next time.
		 */
		for (i = 0;  i < 16;  i++)
			h->out[h->pass_pos + pos + i] =
			    md5[i] ^= h->pass[pos + i];
	}
}

static void
insert_request_authenticator(struct rad_handle *h, int resp)
{
	MD5_CTX ctx;
	const struct rad_server *srvp;

	srvp = &h->servers[h->srv];

	/* Create the request authenticator */
	MD5Init(&ctx);
	MD5Update(&ctx, &h->out[POS_CODE], POS_AUTH - POS_CODE);
	if (resp)
	    MD5Update(&ctx, &h->in[POS_AUTH], LEN_AUTH);
	else
	    MD5Update(&ctx, &h->out[POS_AUTH], LEN_AUTH);
	MD5Update(&ctx, &h->out[POS_ATTRS], h->out_len - POS_ATTRS);
	MD5Update(&ctx, srvp->secret, strlen(srvp->secret));
	MD5Final(&h->out[POS_AUTH], &ctx);
}

static void
insert_message_authenticator(struct rad_handle *h, int resp)
{
#ifdef WITH_SSL
	u_char md[EVP_MAX_MD_SIZE];
	u_int md_len;
	const struct rad_server *srvp;
	HMAC_CTX *ctx;
	srvp = &h->servers[h->srv];

	if (h->authentic_pos != 0) {
		ctx = HMAC_CTX_new();
		HMAC_Init_ex(ctx, srvp->secret, strlen(srvp->secret), EVP_md5(), NULL);
		HMAC_Update(ctx, &h->out[POS_CODE], POS_AUTH - POS_CODE);
		if (resp)
		    HMAC_Update(ctx, &h->in[POS_AUTH], LEN_AUTH);
		else
		    HMAC_Update(ctx, &h->out[POS_AUTH], LEN_AUTH);
		HMAC_Update(ctx, &h->out[POS_ATTRS],
		    h->out_len - POS_ATTRS);
		HMAC_Final(ctx, md, &md_len);
		HMAC_CTX_free(ctx);
		memcpy(&h->out[h->authentic_pos + 2], md, md_len);
	}
#endif
}

/*
 * Return true if the current response is valid for a request to the
 * specified server.
 */
static int
is_valid_response(struct rad_handle *h, int srv,
    const struct sockaddr_in *from)
{
	MD5_CTX ctx;
	unsigned char md5[MD5_DIGEST_LENGTH];
	const struct rad_server *srvp;
	int len;
#ifdef WITH_SSL
	HMAC_CTX *hctx;
	u_char resp[MSGSIZE], md[EVP_MAX_MD_SIZE];
	u_int md_len;
	int pos;
#endif

	srvp = &h->servers[srv];

	/* Check the source address */
	if (from->sin_family != srvp->addr.sin_family ||
	    from->sin_addr.s_addr != srvp->addr.sin_addr.s_addr ||
	    from->sin_port != srvp->addr.sin_port)
		return 0;

	/* Check the message length */
	if (h->in_len < POS_ATTRS)
		return 0;
	len = h->in[POS_LENGTH] << 8 | h->in[POS_LENGTH+1];
	if (len > h->in_len)
		return 0;

	/* Check the response authenticator */
	MD5Init(&ctx);
	MD5Update(&ctx, &h->in[POS_CODE], POS_AUTH - POS_CODE);
	MD5Update(&ctx, &h->out[POS_AUTH], LEN_AUTH);
	MD5Update(&ctx, &h->in[POS_ATTRS], len - POS_ATTRS);
	MD5Update(&ctx, srvp->secret, strlen(srvp->secret));
	MD5Final(md5, &ctx);
	if (memcmp(&h->in[POS_AUTH], md5, sizeof md5) != 0)
		return 0;

#ifdef WITH_SSL
	/*
	 * For non accounting responses check the message authenticator,
	 * if any.
	 */
	if (h->in[POS_CODE] != RAD_ACCOUNTING_RESPONSE) {

		memcpy(resp, h->in, MSGSIZE);
		pos = POS_ATTRS;

		/* Search and verify the Message-Authenticator */
		hctx = HMAC_CTX_new();
		while (pos < len - 2) {

			if (h->in[pos] == RAD_MESSAGE_AUTHENTIC) {
				/* zero fill the Message-Authenticator */
				memset(&resp[pos + 2], 0, MD5_DIGEST_LENGTH);

				HMAC_Init_ex(hctx, srvp->secret,
				    strlen(srvp->secret), EVP_md5(), NULL);
				HMAC_Update(hctx, &h->in[POS_CODE],
				    POS_AUTH - POS_CODE);
				HMAC_Update(hctx, &h->out[POS_AUTH],
				    LEN_AUTH);
				HMAC_Update(hctx, &resp[POS_ATTRS],
				    h->in_len - POS_ATTRS);
				HMAC_Final(hctx, md, &md_len);
				HMAC_CTX_reset(hctx);
				if (memcmp(md, &h->in[pos + 2],
				    MD5_DIGEST_LENGTH) != 0) {
					HMAC_CTX_free(hctx);
					return 0;
				}
				break;
			}
			pos += h->in[pos + 1];
		}
		HMAC_CTX_free(hctx);
	}
#endif
	return 1;
}

/*
 * Return true if the current request is valid for the specified server.
 */
static int
is_valid_request(struct rad_handle *h)
{
	MD5_CTX ctx;
	unsigned char md5[MD5_DIGEST_LENGTH];
	const struct rad_server *srvp;
	int len;
#ifdef WITH_SSL
	HMAC_CTX *hctx;
	u_char resp[MSGSIZE], md[EVP_MAX_MD_SIZE];
	u_int md_len;
	int pos;
#endif

	srvp = &h->servers[h->srv];

	/* Check the message length */
	if (h->in_len < POS_ATTRS)
		return (0);
	len = h->in[POS_LENGTH] << 8 | h->in[POS_LENGTH+1];
	if (len > h->in_len)
		return (0);

	if (h->in[POS_CODE] != RAD_ACCESS_REQUEST) {
		uint32_t zeroes[4] = { 0, 0, 0, 0 };
		/* Check the request authenticator */
		MD5Init(&ctx);
		MD5Update(&ctx, &h->in[POS_CODE], POS_AUTH - POS_CODE);
		MD5Update(&ctx, zeroes, LEN_AUTH);
		MD5Update(&ctx, &h->in[POS_ATTRS], len - POS_ATTRS);
		MD5Update(&ctx, srvp->secret, strlen(srvp->secret));
		MD5Final(md5, &ctx);
		if (memcmp(&h->in[POS_AUTH], md5, sizeof md5) != 0)
			return (0);
	}

#ifdef WITH_SSL
	/* Search and verify the Message-Authenticator */
	pos = POS_ATTRS;
	hctx = HMAC_CTX_new();
	while (pos < len - 2) {
		if (h->in[pos] == RAD_MESSAGE_AUTHENTIC) {
			memcpy(resp, h->in, MSGSIZE);
			/* zero fill the Request-Authenticator */
			if (h->in[POS_CODE] != RAD_ACCESS_REQUEST)
				memset(&resp[POS_AUTH], 0, LEN_AUTH);
			/* zero fill the Message-Authenticator */
			memset(&resp[pos + 2], 0, MD5_DIGEST_LENGTH);

			HMAC_Init_ex(hctx, srvp->secret,
			    strlen(srvp->secret), EVP_md5(), NULL);
			HMAC_Update(hctx, resp, h->in_len);
			HMAC_Final(hctx, md, &md_len);
			HMAC_CTX_reset(hctx);
			if (memcmp(md, &h->in[pos + 2],
			    MD5_DIGEST_LENGTH) != 0) {
				HMAC_CTX_free(hctx);
				return (0);
			}
			break;
		}
		pos += h->in[pos + 1];
	}
	HMAC_CTX_free(hctx);
#endif
	return (1);
}

static int
put_password_attr(struct rad_handle *h, int type, const void *value, size_t len)
{
	int padded_len;
	int pad_len;

	if (h->pass_pos != 0) {
		generr(h, "Multiple User-Password attributes specified");
		return -1;
	}
	if (len > PASSSIZE)
		len = PASSSIZE;
	padded_len = len == 0 ? 16 : (len+15) & ~0xf;
	pad_len = padded_len - len;

	/*
	 * Put in a place-holder attribute containing all zeros, and
	 * remember where it is so we can fill it in later.
	 */
	clear_password(h);
	put_raw_attr(h, type, h->pass, padded_len);
	h->pass_pos = h->out_len - padded_len;

	/* Save the cleartext password, padded as necessary */
	memcpy(h->pass, value, len);
	h->pass_len = len;
	memset(h->pass + len, 0, pad_len);
	return 0;
}

static int
put_raw_attr(struct rad_handle *h, int type, const void *value, size_t len)
{
	if (len > 253) {
		generr(h, "Attribute too long");
		return -1;
	}
	if (h->out_len + 2 + len > MSGSIZE) {
		generr(h, "Maximum message length exceeded");
		return -1;
	}
	h->out[h->out_len++] = type;
	h->out[h->out_len++] = len + 2;
	memcpy(&h->out[h->out_len], value, len);
	h->out_len += len;
	return 0;
}

int
rad_add_server(struct rad_handle *h, const char *host, int port,
    const char *secret, int timeout, int tries)
{
    	struct in_addr bindto;
	bindto.s_addr = INADDR_ANY;

	return rad_add_server_ex(h, host, port, secret, timeout, tries,
		DEAD_TIME, &bindto);
}

int
rad_add_server_ex(struct rad_handle *h, const char *host, int port,
    const char *secret, int timeout, int tries, int dead_time,
    struct in_addr *bindto)
{
	struct rad_server *srvp;

	if (h->num_servers >= MAXSERVERS) {
		generr(h, "Too many RADIUS servers specified");
		return -1;
	}
	srvp = &h->servers[h->num_servers];

	memset(&srvp->addr, 0, sizeof srvp->addr);
	srvp->addr.sin_len = sizeof srvp->addr;
	srvp->addr.sin_family = AF_INET;
	if (!inet_aton(host, &srvp->addr.sin_addr)) {
		struct hostent *hent;

		if ((hent = gethostbyname(host)) == NULL) {
			generr(h, "%s: host not found", host);
			return -1;
		}
		memcpy(&srvp->addr.sin_addr, hent->h_addr,
		    sizeof srvp->addr.sin_addr);
	}
	if (port != 0)
		srvp->addr.sin_port = htons((u_short)port);
	else {
		struct servent *sent;

		if (h->type == RADIUS_AUTH)
			srvp->addr.sin_port =
			    (sent = getservbyname("radius", "udp")) != NULL ?
				sent->s_port : htons(RADIUS_PORT);
		else
			srvp->addr.sin_port =
			    (sent = getservbyname("radacct", "udp")) != NULL ?
				sent->s_port : htons(RADACCT_PORT);
	}
	if ((srvp->secret = strdup(secret)) == NULL) {
		generr(h, "Out of memory");
		return -1;
	}
	srvp->timeout = timeout;
	srvp->max_tries = tries;
	srvp->num_tries = 0;
	srvp->is_dead = 0;
	srvp->dead_time = dead_time;
	srvp->next_probe = 0;
	srvp->bindto = bindto->s_addr;
	h->num_servers++;
	return 0;
}

void
rad_close(struct rad_handle *h)
{
	int srv;

	if (h->fd != -1)
		close(h->fd);
	for (srv = 0;  srv < h->num_servers;  srv++) {
		memset(h->servers[srv].secret, 0,
		    strlen(h->servers[srv].secret));
		free(h->servers[srv].secret);
	}
	clear_password(h);
	free(h);
}

void
rad_bind_to(struct rad_handle *h, in_addr_t addr)
{

	h->bindto = addr;
}

int
rad_config(struct rad_handle *h, const char *path)
{
	FILE *fp;
	char buf[MAXCONFLINE];
	int linenum;
	int retval;

	if (path == NULL)
		path = PATH_RADIUS_CONF;
	if ((fp = fopen(path, "r")) == NULL) {
		generr(h, "Cannot open \"%s\": %s", path, strerror(errno));
		return -1;
	}
	retval = 0;
	linenum = 0;
	while (fgets(buf, sizeof buf, fp) != NULL) {
		int len;
		char *fields[MAX_FIELDS];
		int nfields;
		char msg[ERRSIZE];
		char *type;
		char *host, *res;
		char *port_str;
		char *secret;
		char *timeout_str;
		char *maxtries_str;
		char *dead_time_str;
		char *bindto_str;
		char *end;
		char *wanttype;
		unsigned long timeout;
		unsigned long maxtries;
		unsigned long dead_time;
		int port;
		struct in_addr bindto;
		int i;

		linenum++;
		len = strlen(buf);
		/* We know len > 0, else fgets would have returned NULL. */
		if (buf[len - 1] != '\n') {
			if (len == sizeof buf - 1)
				generr(h, "%s:%d: line too long", path,
				    linenum);
			else
				generr(h, "%s:%d: missing newline", path,
				    linenum);
			retval = -1;
			break;
		}
		buf[len - 1] = '\0';

		/* Extract the fields from the line. */
		nfields = split(buf, fields, MAX_FIELDS, msg, sizeof msg);
		if (nfields == -1) {
			generr(h, "%s:%d: %s", path, linenum, msg);
			retval = -1;
			break;
		}
		if (nfields == 0)
			continue;
		/*
		 * The first field should contain "auth" or "acct" for
		 * authentication or accounting, respectively.  But older
		 * versions of the file didn't have that field.  Default
		 * it to "auth" for backward compatibility.
		 */
		if (strcmp(fields[0], "auth") != 0 &&
		    strcmp(fields[0], "acct") != 0) {
			if (nfields >= MAX_FIELDS) {
				generr(h, "%s:%d: invalid service type", path,
				    linenum);
				retval = -1;
				break;
			}
			nfields++;
			for (i = nfields;  --i > 0;  )
				fields[i] = fields[i - 1];
			fields[0] = "auth";
		}
		if (nfields < 3) {
			generr(h, "%s:%d: missing shared secret", path,
			    linenum);
			retval = -1;
			break;
		}
		type = fields[0];
		host = fields[1];
		secret = fields[2];
		timeout_str = fields[3];
		maxtries_str = fields[4];
		dead_time_str = fields[5];
		bindto_str = fields[6];

		/* Ignore the line if it is for the wrong service type. */
		wanttype = h->type == RADIUS_AUTH ? "auth" : "acct";
		if (strcmp(type, wanttype) != 0)
			continue;

		/* Parse and validate the fields. */
		res = host;
		host = strsep(&res, ":");
		port_str = strsep(&res, ":");
		if (port_str != NULL) {
			port = strtoul(port_str, &end, 10);
			if (*end != '\0') {
				generr(h, "%s:%d: invalid port", path,
				    linenum);
				retval = -1;
				break;
			}
		} else
			port = 0;
		if (timeout_str != NULL) {
			timeout = strtoul(timeout_str, &end, 10);
			if (*end != '\0') {
				generr(h, "%s:%d: invalid timeout", path,
				    linenum);
				retval = -1;
				break;
			}
		} else
			timeout = TIMEOUT;
		if (maxtries_str != NULL) {
			maxtries = strtoul(maxtries_str, &end, 10);
			if (*end != '\0') {
				generr(h, "%s:%d: invalid maxtries", path,
				    linenum);
				retval = -1;
				break;
			}
		} else
			maxtries = MAXTRIES;

		if (dead_time_str != NULL) {
			dead_time = strtoul(dead_time_str, &end, 10);
			if (*end != '\0') {
				generr(h, "%s:%d: invalid dead_time", path,
				    linenum);
				retval = -1;
				break;
			}
		} else
		    	dead_time = DEAD_TIME;

		if (bindto_str != NULL) {
		    	bindto.s_addr = inet_addr(bindto_str);
			if (bindto.s_addr == INADDR_NONE) {
				generr(h, "%s:%d: invalid bindto", path,
				    linenum);
				retval = -1;
				break;
			}
		} else
		    	bindto.s_addr = INADDR_ANY;

		if (rad_add_server_ex(h, host, port, secret, timeout, maxtries,
			    dead_time, &bindto) == -1) {
			strcpy(msg, h->errmsg);
			generr(h, "%s:%d: %s", path, linenum, msg);
			retval = -1;
			break;
		}
	}
	/* Clear out the buffer to wipe a possible copy of a shared secret */
	memset(buf, 0, sizeof buf);
	fclose(fp);
	return retval;
}

/*
 * rad_init_send_request() must have previously been called.
 * Returns:
 *   0     The application should select on *fd with a timeout of tv before
 *         calling rad_continue_send_request again.
 *   < 0   Failure
 *   > 0   Success
 */
int
rad_continue_send_request(struct rad_handle *h, int selected, int *fd,
                          struct timeval *tv)
{
	int n, cur_srv;
	time_t now;
	struct sockaddr_in sin;

	if (h->type == RADIUS_SERVER) {
		generr(h, "denied function call");
		return (-1);
	}
	if (selected) {
		struct sockaddr_in from;
		socklen_t fromlen;

		fromlen = sizeof from;
		h->in_len = recvfrom(h->fd, h->in,
		    MSGSIZE, MSG_WAITALL, (struct sockaddr *)&from, &fromlen);
		if (h->in_len == -1) {
			generr(h, "recvfrom: %s", strerror(errno));
			return -1;
		}
		if (is_valid_response(h, h->srv, &from)) {
			h->in_len = h->in[POS_LENGTH] << 8 |
			    h->in[POS_LENGTH+1];
			h->in_pos = POS_ATTRS;
			return h->in[POS_CODE];
		}
	}

	/*
         * Scan round-robin to the next server that has some
         * tries left.  There is guaranteed to be one, or we
         * would have exited this loop by now.
	 */
	cur_srv = h->srv;
	now = time(NULL);
	if (h->servers[h->srv].num_tries >= h->servers[h->srv].max_tries) {
		/* Set next probe time for this server */
		if (h->servers[h->srv].dead_time) {
			h->servers[h->srv].is_dead = 1;
			h->servers[h->srv].next_probe = now +
			    h->servers[h->srv].dead_time;
		}
		do {
		    	h->srv++;
			if (h->srv >= h->num_servers)
				h->srv = 0;
			if (h->servers[h->srv].is_dead == 0)
			    	break;
			if (h->servers[h->srv].dead_time &&
			    h->servers[h->srv].next_probe <= now) {
			    	h->servers[h->srv].is_dead = 0;
				h->servers[h->srv].num_tries = 0;
				break;
			}
		} while (h->srv != cur_srv);

		if (h->srv == cur_srv) {
			generr(h, "No valid RADIUS responses received");
			return (-1);
		}
	}

	/* Rebind */
	if (h->bindto != h->servers[h->srv].bindto) {
	    	h->bindto = h->servers[h->srv].bindto;
		close(h->fd);
		if ((h->fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
			generr(h, "Cannot create socket: %s", strerror(errno));
			return -1;
		}
		memset(&sin, 0, sizeof sin);
		sin.sin_len = sizeof sin;
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = h->bindto;
		sin.sin_port = 0;
		if (bind(h->fd, (const struct sockaddr *)&sin,
		    sizeof sin) == -1) {
			generr(h, "bind: %s", strerror(errno));
			close(h->fd);
			h->fd = -1;
			return (-1);
		}
	}

	if (h->out[POS_CODE] == RAD_ACCESS_REQUEST) {
		/* Insert the scrambled password into the request */
		if (h->pass_pos != 0)
			insert_scrambled_password(h, h->srv);
	}
	insert_message_authenticator(h, 0);

	if (h->out[POS_CODE] != RAD_ACCESS_REQUEST) {
		/* Insert the request authenticator into the request */
		memset(&h->out[POS_AUTH], 0, LEN_AUTH);
		insert_request_authenticator(h, 0);
	}

	/* Send the request */
	n = sendto(h->fd, h->out, h->out_len, 0,
	    (const struct sockaddr *)&h->servers[h->srv].addr,
	    sizeof h->servers[h->srv].addr);
	if (n != h->out_len)
		tv->tv_sec = 1; /* Do not wait full timeout if send failed. */
	else
		tv->tv_sec = h->servers[h->srv].timeout;
	h->servers[h->srv].num_tries++;
	tv->tv_usec = 0;
	*fd = h->fd;

	return 0;
}

int
rad_receive_request(struct rad_handle *h)
{
	struct sockaddr_in from;
	socklen_t fromlen;
	int n;

	if (h->type != RADIUS_SERVER) {
		generr(h, "denied function call");
		return (-1);
	}
	h->srv = -1;
	fromlen = sizeof(from);
	h->in_len = recvfrom(h->fd, h->in,
	    MSGSIZE, MSG_WAITALL, (struct sockaddr *)&from, &fromlen);
	if (h->in_len == -1) {
		generr(h, "recvfrom: %s", strerror(errno));
		return (-1);
	}
	for (n = 0; n < h->num_servers; n++) {
		if (h->servers[n].addr.sin_addr.s_addr == from.sin_addr.s_addr) {
			h->servers[n].addr.sin_port = from.sin_port;
			h->srv = n;
			break;
		}
	}
	if (h->srv == -1)
		return (-2);
	if (is_valid_request(h)) {
		h->in_len = h->in[POS_LENGTH] << 8 |
		    h->in[POS_LENGTH+1];
		h->in_pos = POS_ATTRS;
		return (h->in[POS_CODE]);
	}
	return (-3);
}

int
rad_send_response(struct rad_handle *h)
{
	int n;

	if (h->type != RADIUS_SERVER) {
		generr(h, "denied function call");
		return (-1);
	}
	/* Fill in the length field in the message */
	h->out[POS_LENGTH] = h->out_len >> 8;
	h->out[POS_LENGTH+1] = h->out_len;

	insert_message_authenticator(h,
	    (h->in[POS_CODE] == RAD_ACCESS_REQUEST) ? 1 : 0);
	insert_request_authenticator(h, 1);

	/* Send the request */
	n = sendto(h->fd, h->out, h->out_len, 0,
	    (const struct sockaddr *)&h->servers[h->srv].addr,
	    sizeof h->servers[h->srv].addr);
	if (n != h->out_len) {
		if (n == -1)
			generr(h, "sendto: %s", strerror(errno));
		else
			generr(h, "sendto: short write");
		return -1;
	}

	return 0;
}

int
rad_create_request(struct rad_handle *h, int code)
{
	int i;

	if (h->type == RADIUS_SERVER) {
		generr(h, "denied function call");
		return (-1);
	}
	if (h->num_servers == 0) {
	    	generr(h, "No RADIUS servers specified");
		return (-1);
	}
	h->out[POS_CODE] = code;
	h->out[POS_IDENT] = ++h->ident;
	if (code == RAD_ACCESS_REQUEST) {
		/* Create a random authenticator */
		for (i = 0;  i < LEN_AUTH;  i += 2) {
			long r;
			r = random();
			h->out[POS_AUTH+i] = (u_char)r;
			h->out[POS_AUTH+i+1] = (u_char)(r >> 8);
		}
	} else
		memset(&h->out[POS_AUTH], 0, LEN_AUTH);
	h->out_len = POS_ATTRS;
	clear_password(h);
	h->authentic_pos = 0;
	h->out_created = 1;
	return 0;
}

int
rad_create_response(struct rad_handle *h, int code)
{

	if (h->type != RADIUS_SERVER) {
		generr(h, "denied function call");
		return (-1);
	}
	h->out[POS_CODE] = code;
	h->out[POS_IDENT] = h->in[POS_IDENT];
	memset(&h->out[POS_AUTH], 0, LEN_AUTH);
	h->out_len = POS_ATTRS;
	clear_password(h);
	h->authentic_pos = 0;
	h->out_created = 1;
	return 0;
}

struct in_addr
rad_cvt_addr(const void *data)
{
	struct in_addr value;

	memcpy(&value.s_addr, data, sizeof value.s_addr);
	return value;
}

struct in6_addr
rad_cvt_addr6(const void *data)
{
	struct in6_addr value;

	memcpy(&value.s6_addr, data, sizeof value.s6_addr);
	return value;
}

u_int32_t
rad_cvt_int(const void *data)
{
	u_int32_t value;

	memcpy(&value, data, sizeof value);
	return ntohl(value);
}

char *
rad_cvt_string(const void *data, size_t len)
{
	char *s;

	s = malloc(len + 1);
	if (s != NULL) {
		memcpy(s, data, len);
		s[len] = '\0';
	}
	return s;
}

/*
 * Returns the attribute type.  If none are left, returns 0.  On failure,
 * returns -1.
 */
int
rad_get_attr(struct rad_handle *h, const void **value, size_t *len)
{
	int type;

	if (h->in_pos >= h->in_len)
		return 0;
	if (h->in_pos + 2 > h->in_len) {
		generr(h, "Malformed attribute in response");
		return -1;
	}
	type = h->in[h->in_pos++];
	*len = h->in[h->in_pos++] - 2;
	if (h->in_pos + (int)*len > h->in_len) {
		generr(h, "Malformed attribute in response");
		return -1;
	}
	*value = &h->in[h->in_pos];
	h->in_pos += *len;
	return type;
}

/*
 * Returns -1 on error, 0 to indicate no event and >0 for success
 */
int
rad_init_send_request(struct rad_handle *h, int *fd, struct timeval *tv)
{
	int srv;
	time_t now;
	struct sockaddr_in sin;

	if (h->type == RADIUS_SERVER) {
		generr(h, "denied function call");
		return (-1);
	}
	/* Make sure we have a socket to use */
	if (h->fd == -1) {
		if ((h->fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
			generr(h, "Cannot create socket: %s", strerror(errno));
			return -1;
		}
		memset(&sin, 0, sizeof sin);
		sin.sin_len = sizeof sin;
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = h->bindto;
		sin.sin_port = htons(0);
		if (bind(h->fd, (const struct sockaddr *)&sin,
		    sizeof sin) == -1) {
			generr(h, "bind: %s", strerror(errno));
			close(h->fd);
			h->fd = -1;
			return -1;
		}
	}

	if (h->out[POS_CODE] != RAD_ACCESS_REQUEST) {
		/* Make sure no password given */
		if (h->pass_pos || h->chap_pass) {
			generr(h, "User or Chap Password"
			    " in accounting request");
			return -1;
		}
	} else {
		if (h->eap_msg == 0) {
			/* Make sure the user gave us a password */
			if (h->pass_pos == 0 && !h->chap_pass) {
				generr(h, "No User or Chap Password"
				    " attributes given");
				return -1;
			}
			if (h->pass_pos != 0 && h->chap_pass) {
				generr(h, "Both User and Chap Password"
				    " attributes given");
				return -1;
			}
		}
	}

	/* Fill in the length field in the message */
	h->out[POS_LENGTH] = h->out_len >> 8;
	h->out[POS_LENGTH+1] = h->out_len;

	h->srv = 0;
	now = time(NULL);
	for (srv = 0;  srv < h->num_servers;  srv++)
		h->servers[srv].num_tries = 0;
	/* Find a first good server. */
	for (srv = 0;  srv < h->num_servers;  srv++) {
		if (h->servers[srv].is_dead == 0)
			break;
		if (h->servers[srv].dead_time &&
		    h->servers[srv].next_probe <= now) {
		    	h->servers[srv].is_dead = 0;
			break;
		}
		h->srv++;
	}

	/* If all servers was dead on the last probe, try from beginning */
	if (h->srv == h->num_servers) {
		for (srv = 0;  srv < h->num_servers;  srv++) {
		    	h->servers[srv].is_dead = 0;
			h->servers[srv].next_probe = 0;
		}
		h->srv = 0;
	}

	return rad_continue_send_request(h, 0, fd, tv);
}

/*
 * Create and initialize a rad_handle structure, and return it to the
 * caller.  Can fail only if the necessary memory cannot be allocated.
 * In that case, it returns NULL.
 */
struct rad_handle *
rad_auth_open(void)
{
	struct rad_handle *h;

	h = (struct rad_handle *)malloc(sizeof(struct rad_handle));
	if (h != NULL) {
		srandomdev();
		h->fd = -1;
		h->num_servers = 0;
		h->ident = random();
		h->errmsg[0] = '\0';
		memset(h->pass, 0, sizeof h->pass);
		h->pass_len = 0;
		h->pass_pos = 0;
		h->chap_pass = 0;
		h->authentic_pos = 0;
		h->type = RADIUS_AUTH;
		h->out_created = 0;
		h->eap_msg = 0;
		h->bindto = INADDR_ANY;
	}
	return h;
}

struct rad_handle *
rad_acct_open(void)
{
	struct rad_handle *h;

	h = rad_open();
	if (h != NULL)
	        h->type = RADIUS_ACCT;
	return h;
}

struct rad_handle *
rad_server_open(int fd)
{
	struct rad_handle *h;

	h = rad_open();
	if (h != NULL) {
	        h->type = RADIUS_SERVER;
	        h->fd = fd;
	}
	return h;
}

struct rad_handle *
rad_open(void)
{
    return rad_auth_open();
}

int
rad_put_addr(struct rad_handle *h, int type, struct in_addr addr)
{
	return rad_put_attr(h, type, &addr.s_addr, sizeof addr.s_addr);
}

int
rad_put_addr6(struct rad_handle *h, int type, struct in6_addr addr)
{

	return rad_put_attr(h, type, &addr.s6_addr, sizeof addr.s6_addr);
}

int
rad_put_attr(struct rad_handle *h, int type, const void *value, size_t len)
{
	int result;

	if (!h->out_created) {
		generr(h, "Please call rad_create_request()"
		    " before putting attributes");
		return -1;
	}

	if (h->out[POS_CODE] == RAD_ACCOUNTING_REQUEST) {
		if (type == RAD_EAP_MESSAGE) {
			generr(h, "EAP-Message attribute is not valid"
			    " in accounting requests");
			return -1;
		}
	}

	/*
	 * When proxying EAP Messages, the Message Authenticator
	 * MUST be present; see RFC 3579.
	 */
	if (type == RAD_EAP_MESSAGE) {
		if (rad_put_message_authentic(h) == -1)
			return -1;
	}

	if (type == RAD_USER_PASSWORD) {
		result = put_password_attr(h, type, value, len);
	} else if (type == RAD_MESSAGE_AUTHENTIC) {
		result = rad_put_message_authentic(h);
	} else {
		result = put_raw_attr(h, type, value, len);
		if (result == 0) {
			if (type == RAD_CHAP_PASSWORD)
				h->chap_pass = 1;
			else if (type == RAD_EAP_MESSAGE)
				h->eap_msg = 1;
		}
	}

	return result;
}

int
rad_put_int(struct rad_handle *h, int type, u_int32_t value)
{
	u_int32_t nvalue;

	nvalue = htonl(value);
	return rad_put_attr(h, type, &nvalue, sizeof nvalue);
}

int
rad_put_string(struct rad_handle *h, int type, const char *str)
{
	return rad_put_attr(h, type, str, strlen(str));
}

int
rad_put_message_authentic(struct rad_handle *h)
{
#ifdef WITH_SSL
	u_char md_zero[MD5_DIGEST_LENGTH];

	if (h->out[POS_CODE] == RAD_ACCOUNTING_REQUEST) {
		generr(h, "Message-Authenticator is not valid"
		    " in accounting requests");
		return -1;
	}

	if (h->authentic_pos == 0) {
		h->authentic_pos = h->out_len;
		memset(md_zero, 0, sizeof(md_zero));
		return (put_raw_attr(h, RAD_MESSAGE_AUTHENTIC, md_zero,
		    sizeof(md_zero)));
	}
	return 0;
#else
	generr(h, "Message Authenticator not supported,"
	    " please recompile libradius with SSL support");
	return -1;
#endif
}

/*
 * Returns the response type code on success, or -1 on failure.
 */
int
rad_send_request(struct rad_handle *h)
{
	struct timeval timelimit;
	struct timeval tv;
	int fd;
	int n;

	n = rad_init_send_request(h, &fd, &tv);

	if (n != 0)
		return n;

	gettimeofday(&timelimit, NULL);
	timeradd(&tv, &timelimit, &timelimit);

	for ( ; ; ) {
		fd_set readfds;

		FD_ZERO(&readfds);
		FD_SET(fd, &readfds);

		n = select(fd + 1, &readfds, NULL, NULL, &tv);

		if (n == -1) {
			generr(h, "select: %s", strerror(errno));
			return -1;
		}

		if (!FD_ISSET(fd, &readfds)) {
			/* Compute a new timeout */
			gettimeofday(&tv, NULL);
			timersub(&timelimit, &tv, &tv);
			if (tv.tv_sec > 0 || (tv.tv_sec == 0 && tv.tv_usec > 0))
				/* Continue the select */
				continue;
		}

		n = rad_continue_send_request(h, n, &fd, &tv);

		if (n != 0)
			return n;

		gettimeofday(&timelimit, NULL);
		timeradd(&tv, &timelimit, &timelimit);
	}
}

const char *
rad_strerror(struct rad_handle *h)
{
	return h->errmsg;
}

/*
 * Destructively split a string into fields separated by white space.
 * `#' at the beginning of a field begins a comment that extends to the
 * end of the string.  Fields may be quoted with `"'.  Inside quoted
 * strings, the backslash escapes `\"' and `\\' are honored.
 *
 * Pointers to up to the first maxfields fields are stored in the fields
 * array.  Missing fields get NULL pointers.
 *
 * The return value is the actual number of fields parsed, and is always
 * <= maxfields.
 *
 * On a syntax error, places a message in the msg string, and returns -1.
 */
static int
split(char *str, char *fields[], int maxfields, char *msg, size_t msglen)
{
	char *p;
	int i;
	static const char ws[] = " \t";

	for (i = 0;  i < maxfields;  i++)
		fields[i] = NULL;
	p = str;
	i = 0;
	while (*p != '\0') {
		p += strspn(p, ws);
		if (*p == '#' || *p == '\0')
			break;
		if (i >= maxfields) {
			snprintf(msg, msglen, "line has too many fields");
			return -1;
		}
		if (*p == '"') {
			char *dst;

			dst = ++p;
			fields[i] = dst;
			while (*p != '"') {
				if (*p == '\\') {
					p++;
					if (*p != '"' && *p != '\\' &&
					    *p != '\0') {
						snprintf(msg, msglen,
						    "invalid `\\' escape");
						return -1;
					}
				}
				if (*p == '\0') {
					snprintf(msg, msglen,
					    "unterminated quoted string");
					return -1;
				}
				*dst++ = *p++;
			}
			*dst = '\0';
			p++;
			if (*fields[i] == '\0') {
				snprintf(msg, msglen,
				    "empty quoted string not permitted");
				return -1;
			}
			if (*p != '\0' && strspn(p, ws) == 0) {
				snprintf(msg, msglen, "quoted string not"
				    " followed by white space");
				return -1;
			}
		} else {
			fields[i] = p;
			p += strcspn(p, ws);
			if (*p != '\0')
				*p++ = '\0';
		}
		i++;
	}
	return i;
}

int
rad_get_vendor_attr(u_int32_t *vendor, const void **data, size_t *len)
{
	struct vendor_attribute *attr;

	attr = (struct vendor_attribute *)*data;
	*vendor = ntohl(attr->vendor_value);
	*data = attr->attrib_data;
	*len = attr->attrib_len - 2;

	return (attr->attrib_type);
}

int
rad_put_vendor_addr(struct rad_handle *h, int vendor, int type,
    struct in_addr addr)
{
	return (rad_put_vendor_attr(h, vendor, type, &addr.s_addr,
	    sizeof addr.s_addr));
}

int
rad_put_vendor_addr6(struct rad_handle *h, int vendor, int type,
    struct in6_addr addr)
{

	return (rad_put_vendor_attr(h, vendor, type, &addr.s6_addr,
	    sizeof addr.s6_addr));
}

int
rad_put_vendor_attr(struct rad_handle *h, int vendor, int type,
    const void *value, size_t len)
{
	struct vendor_attribute *attr;
	int res;

	if (!h->out_created) {
		generr(h, "Please call rad_create_request()"
		    " before putting attributes");
		return -1;
	}

	if ((attr = malloc(len + 6)) == NULL) {
		generr(h, "malloc failure (%zu bytes)", len + 6);
		return -1;
	}

	attr->vendor_value = htonl(vendor);
	attr->attrib_type = type;
	attr->attrib_len = len + 2;
	memcpy(attr->attrib_data, value, len);

	res = put_raw_attr(h, RAD_VENDOR_SPECIFIC, attr, len + 6);
	free(attr);
	if (res == 0 && vendor == RAD_VENDOR_MICROSOFT
	    && (type == RAD_MICROSOFT_MS_CHAP_RESPONSE
	    || type == RAD_MICROSOFT_MS_CHAP2_RESPONSE)) {
		h->chap_pass = 1;
	}
	return (res);
}

int
rad_put_vendor_int(struct rad_handle *h, int vendor, int type, u_int32_t i)
{
	u_int32_t value;

	value = htonl(i);
	return (rad_put_vendor_attr(h, vendor, type, &value, sizeof value));
}

int
rad_put_vendor_string(struct rad_handle *h, int vendor, int type,
    const char *str)
{
	return (rad_put_vendor_attr(h, vendor, type, str, strlen(str)));
}

ssize_t
rad_request_authenticator(struct rad_handle *h, char *buf, size_t len)
{
	if (len < LEN_AUTH)
		return (-1);
	memcpy(buf, h->out + POS_AUTH, LEN_AUTH);
	if (len > LEN_AUTH)
		buf[LEN_AUTH] = '\0';
	return (LEN_AUTH);
}

u_char *
rad_demangle(struct rad_handle *h, const void *mangled, size_t mlen)
{
	char R[LEN_AUTH];
	const char *S;
	int i, Ppos;
	MD5_CTX Context;
	u_char b[MD5_DIGEST_LENGTH], *C, *demangled;

	if ((mlen % 16 != 0) || mlen > 128) {
		generr(h, "Cannot interpret mangled data of length %lu",
		    (u_long)mlen);
		return NULL;
	}

	C = (u_char *)mangled;

	/* We need the shared secret as Salt */
	S = rad_server_secret(h);

	/* We need the request authenticator */
	if (rad_request_authenticator(h, R, sizeof R) != LEN_AUTH) {
		generr(h, "Cannot obtain the RADIUS request authenticator");
		return NULL;
	}

	demangled = malloc(mlen);
	if (!demangled)
		return NULL;

	MD5Init(&Context);
	MD5Update(&Context, S, strlen(S));
	MD5Update(&Context, R, LEN_AUTH);
	MD5Final(b, &Context);
	Ppos = 0;
	while (mlen) {

		mlen -= 16;
		for (i = 0; i < 16; i++)
			demangled[Ppos++] = C[i] ^ b[i];

		if (mlen) {
			MD5Init(&Context);
			MD5Update(&Context, S, strlen(S));
			MD5Update(&Context, C, 16);
			MD5Final(b, &Context);
		}

		C += 16;
	}

	return demangled;
}

u_char *
rad_demangle_mppe_key(struct rad_handle *h, const void *mangled,
    size_t mlen, size_t *len)
{
	char R[LEN_AUTH];    /* variable names as per rfc2548 */
	const char *S;
	u_char b[MD5_DIGEST_LENGTH], *demangled;
	const u_char *A, *C;
	MD5_CTX Context;
	int Slen, i, Clen, Ppos;
	u_char *P;

	if (mlen % 16 != SALT_LEN) {
		generr(h, "Cannot interpret mangled data of length %lu",
		    (u_long)mlen);
		return NULL;
	}

	/* We need the RADIUS Request-Authenticator */
	if (rad_request_authenticator(h, R, sizeof R) != LEN_AUTH) {
		generr(h, "Cannot obtain the RADIUS request authenticator");
		return NULL;
	}

	A = (const u_char *)mangled;      /* Salt comes first */
	C = (const u_char *)mangled + SALT_LEN;  /* Then the ciphertext */
	Clen = mlen - SALT_LEN;
	S = rad_server_secret(h);    /* We need the RADIUS secret */
	Slen = strlen(S);
	P = alloca(Clen);        /* We derive our plaintext */

	MD5Init(&Context);
	MD5Update(&Context, S, Slen);
	MD5Update(&Context, R, LEN_AUTH);
	MD5Update(&Context, A, SALT_LEN);
	MD5Final(b, &Context);
	Ppos = 0;

	while (Clen) {
		Clen -= 16;

		for (i = 0; i < 16; i++)
		    P[Ppos++] = C[i] ^ b[i];

		if (Clen) {
			MD5Init(&Context);
			MD5Update(&Context, S, Slen);
			MD5Update(&Context, C, 16);
			MD5Final(b, &Context);
		}

		C += 16;
	}

	/*
	* The resulting plain text consists of a one-byte length, the text and
	* maybe some padding.
	*/
	*len = *P;
	if (*len > mlen - 1) {
		generr(h, "Mangled data seems to be garbage %zu %zu",
		    *len, mlen-1);
		return NULL;
	}

	if (*len > MPPE_KEY_LEN * 2) {
		generr(h, "Key to long (%zu) for me max. %d",
		    *len, MPPE_KEY_LEN * 2);
		return NULL;
	}
	demangled = malloc(*len);
	if (!demangled)
		return NULL;

	memcpy(demangled, P + 1, *len);
	return demangled;
}

const char *
rad_server_secret(struct rad_handle *h)
{
	return (h->servers[h->srv].secret);
}
