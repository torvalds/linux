/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998, 2001, 2002, Juniper Networks, Inc.
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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <md5.h>
#include <netdb.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "taclib_private.h"

static int		 add_str_8(struct tac_handle *, u_int8_t *,
			    struct clnt_str *);
static int		 add_str_16(struct tac_handle *, u_int16_t *,
			    struct clnt_str *);
static int		 protocol_version(int, int, int);
static void		 close_connection(struct tac_handle *);
static int		 conn_server(struct tac_handle *);
static void		 crypt_msg(struct tac_handle *, struct tac_msg *);
static void		*dup_str(struct tac_handle *, const struct srvr_str *,
			    size_t *);
static int		 establish_connection(struct tac_handle *);
static void		 free_str(struct clnt_str *);
static void		 generr(struct tac_handle *, const char *, ...)
			    __printflike(2, 3);
static void		 gen_session_id(struct tac_msg *);
static int		 get_srvr_end(struct tac_handle *);
static int		 get_srvr_str(struct tac_handle *, const char *,
				      struct srvr_str *, size_t);
static void		 init_clnt_str(struct clnt_str *);
static void		 init_srvr_str(struct srvr_str *);
static int		 read_timed(struct tac_handle *, void *, size_t,
			    const struct timeval *);
static int		 recv_msg(struct tac_handle *);
static int		 save_str(struct tac_handle *, struct clnt_str *,
			    const void *, size_t);
static int		 send_msg(struct tac_handle *);
static int		 split(char *, char *[], int, char *, size_t);
static void		*xmalloc(struct tac_handle *, size_t);
static char		*xstrdup(struct tac_handle *, const char *);
static void              clear_srvr_avs(struct tac_handle *);
static void              create_msg(struct tac_handle *, int, int, int);

/*
 * Append some optional data to the current request, and store its
 * length into the 8-bit field referenced by "fld".  Returns 0 on
 * success, or -1 on failure.
 *
 * This function also frees the "cs" string data and initializes it
 * for the next time.
 */
static int
add_str_8(struct tac_handle *h, u_int8_t *fld, struct clnt_str *cs)
{
	u_int16_t len;

	if (add_str_16(h, &len, cs) == -1)
		return -1;
	len = ntohs(len);
	if (len > 0xff) {
		generr(h, "Field too long");
		return -1;
	}
	*fld = len;
	return 0;
}

/*
 * Append some optional data to the current request, and store its
 * length into the 16-bit field (network byte order) referenced by
 * "fld".  Returns 0 on success, or -1 on failure.
 *
 * This function also frees the "cs" string data and initializes it
 * for the next time.
 */
static int
add_str_16(struct tac_handle *h, u_int16_t *fld, struct clnt_str *cs)
{
	size_t len;

	len = cs->len;
	if (cs->data == NULL)
		len = 0;
	if (len != 0) {
		int offset;

		if (len > 0xffff) {
			generr(h, "Field too long");
			return -1;
		}
		offset = ntohl(h->request.length);
		if (offset + len > BODYSIZE) {
			generr(h, "Message too long");
			return -1;
		}
		memcpy(h->request.u.body + offset, cs->data, len);
		h->request.length = htonl(offset + len);
	}
	*fld = htons(len);
	free_str(cs);
	return 0;
}

static int
protocol_version(int msg_type, int var, int type)
{
    int minor;

    switch (msg_type) {
        case TAC_AUTHEN:
	    /* 'var' represents the 'action' */
	    switch (var) {
	        case TAC_AUTHEN_LOGIN:
		    switch (type) {

		        case TAC_AUTHEN_TYPE_PAP:
			case TAC_AUTHEN_TYPE_CHAP:
			case TAC_AUTHEN_TYPE_MSCHAP:
			case TAC_AUTHEN_TYPE_ARAP:
			    minor = 1;
			break;

			default:
			    minor = 0;
			break;
		     }
		break;

		case TAC_AUTHEN_SENDAUTH:
		    minor = 1;
		break;

		default:
		    minor = 0;
		break;
	    };
	break;

	case TAC_AUTHOR:
	    /* 'var' represents the 'method' */
	    switch (var) {
	        /*
		 * When new authentication methods are added, include 'method'
		 * in determining the value of 'minor'.  At this point, all
                 * methods defined in this implementation (see "Authorization 
                 * authentication methods" in taclib.h) are minor version 0
		 * Not all types, however, indicate minor version 0.
		 */
                case TAC_AUTHEN_METH_NOT_SET:
                case TAC_AUTHEN_METH_NONE:
                case TAC_AUTHEN_METH_KRB5:
                case TAC_AUTHEN_METH_LINE:
                case TAC_AUTHEN_METH_ENABLE:
                case TAC_AUTHEN_METH_LOCAL:
                case TAC_AUTHEN_METH_TACACSPLUS:
                case TAC_AUTHEN_METH_RCMD:
		    switch (type) {
		        case TAC_AUTHEN_TYPE_PAP:
			case TAC_AUTHEN_TYPE_CHAP:
			case TAC_AUTHEN_TYPE_MSCHAP:
			case TAC_AUTHEN_TYPE_ARAP:
			    minor = 1;
			break;

			default:
			    minor = 0;
			break;
		     }
	        break;
	        default:
		    minor = 0;
		break;
	    }
        break;

	case TAC_ACCT:

	default:
	    minor = 0;
        break;
    }

    return TAC_VER_MAJOR << 4 | minor;
}


static void
close_connection(struct tac_handle *h)
{
	if (h->fd != -1) {
		close(h->fd);
		h->fd = -1;
	}
}

static int
conn_server(struct tac_handle *h)
{
	const struct tac_server *srvp = &h->servers[h->cur_server];
	int flags;

	if ((h->fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		generr(h, "Cannot create socket: %s", strerror(errno));
		return -1;
	}
	if ((flags = fcntl(h->fd, F_GETFL, 0)) == -1 ||
	    fcntl(h->fd, F_SETFL, flags | O_NONBLOCK) == -1) {
		generr(h, "Cannot set non-blocking mode on socket: %s",
		    strerror(errno));
		close(h->fd);
		h->fd = -1;
		return -1;
	}
	if (connect(h->fd, (struct sockaddr *)&srvp->addr,
	    sizeof srvp->addr) == 0)
		return 0;

	if (errno == EINPROGRESS) {
		fd_set wfds;
		struct timeval tv;
		int nfds;
		struct sockaddr peer;
		socklen_t errlen, peerlen;
		int err;

		/* Wait for the connection to complete. */
		FD_ZERO(&wfds);
		FD_SET(h->fd, &wfds);
		tv.tv_sec = srvp->timeout;
		tv.tv_usec = 0;
		nfds = select(h->fd + 1, NULL, &wfds, NULL, &tv);
		if (nfds == -1) {
			generr(h, "select: %s", strerror(errno));
			close(h->fd);
			h->fd = -1;
			return -1;
		}
		if (nfds == 0) {
			generr(h, "connect: timed out");
			close(h->fd);
			h->fd = -1;
			return -1;
		}

		/* See whether we are connected now. */
		peerlen = sizeof peer;
		if (getpeername(h->fd, &peer, &peerlen) == 0)
			return 0;

		if (errno != ENOTCONN) {
			generr(h, "getpeername: %s", strerror(errno));
			close(h->fd);
			h->fd = -1;
			return -1;
		}

		/* Find out why the connect failed. */
		errlen = sizeof err;
		getsockopt(h->fd, SOL_SOCKET, SO_ERROR, &err, &errlen);
		errno = err;
	}
	generr(h, "connect: %s", strerror(errno));
	close(h->fd);
	h->fd = -1;
	return -1;
}

/*
 * Encrypt or decrypt a message.  The operations are symmetrical.
 */
static void
crypt_msg(struct tac_handle *h, struct tac_msg *msg)
{
	const char *secret;
	MD5_CTX base_ctx;
	MD5_CTX ctx;
	unsigned char md5[16];
	int chunk;
	int msg_len;

	secret = h->servers[h->cur_server].secret;
	if (secret[0] == '\0')
		msg->flags |= TAC_UNENCRYPTED;
	if (msg->flags & TAC_UNENCRYPTED)
		return;

	msg_len = ntohl(msg->length);

	MD5Init(&base_ctx);
	MD5Update(&base_ctx, msg->session_id, sizeof msg->session_id);
	MD5Update(&base_ctx, secret, strlen(secret));
	MD5Update(&base_ctx, &msg->version, sizeof msg->version);
	MD5Update(&base_ctx, &msg->seq_no, sizeof msg->seq_no);

	ctx = base_ctx;
	for (chunk = 0;  chunk < msg_len;  chunk += sizeof md5) {
		int chunk_len;
		int i;

		MD5Final(md5, &ctx);

		if ((chunk_len = msg_len - chunk) > sizeof md5)
			chunk_len = sizeof md5;
		for (i = 0;  i < chunk_len;  i++)
			msg->u.body[chunk + i] ^= md5[i];

		ctx = base_ctx;
		MD5Update(&ctx, md5, sizeof md5);
	}
}

/*
 * Return a dynamically allocated copy of the given server string.
 * The copy is null-terminated.  If "len" is non-NULL, the length of
 * the string (excluding the terminating null byte) is stored via it.
 * Returns NULL on failure.  Empty strings are still allocated even
 * though they have no content.
 */
static void *
dup_str(struct tac_handle *h, const struct srvr_str *ss, size_t *len)
{
	unsigned char *p;

	if ((p = (unsigned char *)xmalloc(h, ss->len + 1)) == NULL)
		return NULL;
	if (ss->data != NULL && ss->len != 0)
		memcpy(p, ss->data, ss->len);
	p[ss->len] = '\0';
	if (len != NULL)
		*len = ss->len;
	return p;
}

static int
establish_connection(struct tac_handle *h)
{
	int i;

	if (h->fd >= 0)		/* Already connected. */
		return 0;
	if (h->num_servers == 0) {
		generr(h, "No TACACS+ servers specified");
		return -1;
	}
	/*
         * Try the servers round-robin.  We begin with the one that
         * worked for us the last time.  That way, once we find a good
         * server, we won't waste any more time trying the bad ones.
	 */
	for (i = 0;  i < h->num_servers;  i++) {
		if (conn_server(h) == 0) {
			h->single_connect = (h->servers[h->cur_server].flags &
			    TAC_SRVR_SINGLE_CONNECT) != 0;
			return 0;
		}
		if (++h->cur_server >= h->num_servers)	/* Wrap around */
			h->cur_server = 0;
	}
	/* Just return whatever error was last reported by conn_server(). */
	return -1;
}

/*
 * Free a client string, obliterating its contents first for security.
 */
static void
free_str(struct clnt_str *cs)
{
	if (cs->data != NULL) {
		memset(cs->data, 0, cs->len);
		free(cs->data);
		cs->data = NULL;
		cs->len = 0;
	}
}

static void
generr(struct tac_handle *h, const char *format, ...)
{
	va_list		 ap;

	va_start(ap, format);
	vsnprintf(h->errmsg, ERRSIZE, format, ap);
	va_end(ap);
}

static void
gen_session_id(struct tac_msg *msg)
{
	int r;

	r = random();
	msg->session_id[0] = r >> 8;
	msg->session_id[1] = r;
	r = random();
	msg->session_id[2] = r >> 8;
	msg->session_id[3] = r;
}

/*
 * Verify that we are exactly at the end of the response message.
 * Returns 0 on success, -1 on failure.
 */
static int
get_srvr_end(struct tac_handle *h)
{
	int len;

	len = ntohl(h->response.length);

	if (h->srvr_pos != len) {
		generr(h, "Invalid length field in response "
		       "from server: end expected at %u, response length %u",
		       h->srvr_pos, len);
		return -1;
	}
	return 0;
}

static int
get_srvr_str(struct tac_handle *h, const char *field,
	     struct srvr_str *ss, size_t len)
{
	if (h->srvr_pos + len > ntohl(h->response.length)) {
		generr(h, "Invalid length field in %s response from server "
		       "(%lu > %lu)", field, (u_long)(h->srvr_pos + len),
		       (u_long)ntohl(h->response.length));
		return -1;
	}
	ss->data = len != 0 ? h->response.u.body + h->srvr_pos : NULL;
	ss->len = len;
	h->srvr_pos += len;
	return 0;
}

static void
init_clnt_str(struct clnt_str *cs)
{
	cs->data = NULL;
	cs->len = 0;
}

static void
init_srvr_str(struct srvr_str *ss)
{
	ss->data = NULL;
	ss->len = 0;
}

static int
read_timed(struct tac_handle *h, void *buf, size_t len,
    const struct timeval *deadline)
{
	char *ptr;

	ptr = (char *)buf;
	while (len > 0) {
		int n;

		n = read(h->fd, ptr, len);
		if (n == -1) {
			struct timeval tv;
			int nfds;

			if (errno != EAGAIN) {
				generr(h, "Network read error: %s",
				    strerror(errno));
				return -1;
			}

			/* Wait until we can read more data. */
			gettimeofday(&tv, NULL);
			timersub(deadline, &tv, &tv);
			if (tv.tv_sec >= 0) {
				fd_set rfds;

				FD_ZERO(&rfds);
				FD_SET(h->fd, &rfds);
				nfds =
				    select(h->fd + 1, &rfds, NULL, NULL, &tv);
				if (nfds == -1) {
					generr(h, "select: %s",
					    strerror(errno));
					return -1;
				}
			} else
				nfds = 0;
			if (nfds == 0) {
				generr(h, "Network read timed out");
				return -1;
			}
		} else if (n == 0) {
			generr(h, "unexpected EOF from server");
			return -1;
		} else {
			ptr += n;
			len -= n;
		}
	}
	return 0;
}

/*
 * Receive a response from the server and decrypt it.  Returns 0 on
 * success, or -1 on failure.
 */
static int
recv_msg(struct tac_handle *h)
{
	struct timeval deadline;
	struct tac_msg *msg;
	u_int32_t len;

	msg = &h->response;
	gettimeofday(&deadline, NULL);
	deadline.tv_sec += h->servers[h->cur_server].timeout;

	/* Read the message header and make sure it is reasonable. */
	if (read_timed(h, msg, HDRSIZE, &deadline) == -1)
		return -1;
	if (memcmp(msg->session_id, h->request.session_id,
	    sizeof msg->session_id) != 0) {
		generr(h, "Invalid session ID in received message");
		return -1;
	}
	if (msg->type != h->request.type) {
		generr(h, "Invalid type in received message"
			  " (got %u, expected %u)",
			  msg->type, h->request.type);
		return -1;
	}
	len = ntohl(msg->length);
	if (len > BODYSIZE) {
		generr(h, "Received message too large (%u > %u)",
			  len, BODYSIZE);
		return -1;
	}
	if (msg->seq_no != ++h->last_seq_no) {
		generr(h, "Invalid sequence number in received message"
			  " (got %u, expected %u)",
			  msg->seq_no, h->last_seq_no);
		return -1;
	}

	/* Read the message body. */
	if (read_timed(h, msg->u.body, len, &deadline) == -1)
		return -1;

	/* Decrypt it. */
	crypt_msg(h, msg);

	/*
	 * Turn off single-connection mode if the server isn't amenable
	 * to it.
	 */
	if (!(msg->flags & TAC_SINGLE_CONNECT))
		h->single_connect = 0;
	return 0;
}

static int
save_str(struct tac_handle *h, struct clnt_str *cs, const void *data,
    size_t len)
{
	free_str(cs);
	if (data != NULL && len != 0) {
		if ((cs->data = xmalloc(h, len)) == NULL)
			return -1;
		cs->len = len;
		memcpy(cs->data, data, len);
	}
	return 0;
}

/*
 * Send the current request, after encrypting it.  Returns 0 on success,
 * or -1 on failure.
 */
static int
send_msg(struct tac_handle *h)
{
	struct timeval deadline;
	struct tac_msg *msg;
	char *ptr;
	int len;

	if (h->last_seq_no & 1) {
		generr(h, "Attempt to send message out of sequence");
		return -1;
	}

	if (establish_connection(h) == -1)
		return -1;

	msg = &h->request;
	msg->seq_no = ++h->last_seq_no;
	if (msg->seq_no == 1)
		gen_session_id(msg);
	crypt_msg(h, msg);

	if (h->single_connect)
		msg->flags |= TAC_SINGLE_CONNECT;
	else
		msg->flags &= ~TAC_SINGLE_CONNECT;
	gettimeofday(&deadline, NULL);
	deadline.tv_sec += h->servers[h->cur_server].timeout;
	len = HDRSIZE + ntohl(msg->length);
	ptr = (char *)msg;
	while (len > 0) {
		int n;

		n = write(h->fd, ptr, len);
		if (n == -1) {
			struct timeval tv;
			int nfds;

			if (errno != EAGAIN) {
				generr(h, "Network write error: %s",
				    strerror(errno));
				return -1;
			}

			/* Wait until we can write more data. */
			gettimeofday(&tv, NULL);
			timersub(&deadline, &tv, &tv);
			if (tv.tv_sec >= 0) {
				fd_set wfds;

				FD_ZERO(&wfds);
				FD_SET(h->fd, &wfds);
				nfds =
				    select(h->fd + 1, NULL, &wfds, NULL, &tv);
				if (nfds == -1) {
					generr(h, "select: %s",
					    strerror(errno));
					return -1;
				}
			} else
				nfds = 0;
			if (nfds == 0) {
				generr(h, "Network write timed out");
				return -1;
			}
		} else {
			ptr += n;
			len -= n;
		}
	}
	return 0;
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
tac_add_server(struct tac_handle *h, const char *host, int port,
    const char *secret, int timeout, int flags)
{
	struct tac_server *srvp;

	if (h->num_servers >= MAXSERVERS) {
		generr(h, "Too many TACACS+ servers specified");
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
	srvp->addr.sin_port = htons(port != 0 ? port : TACPLUS_PORT);
	if ((srvp->secret = xstrdup(h, secret)) == NULL)
		return -1;
	srvp->timeout = timeout;
	srvp->flags = flags;
	h->num_servers++;
	return 0;
}

void
tac_close(struct tac_handle *h)
{
	int i, srv;

	if (h->fd != -1)
		close(h->fd);
	for (srv = 0;  srv < h->num_servers;  srv++) {
		memset(h->servers[srv].secret, 0,
		    strlen(h->servers[srv].secret));
		free(h->servers[srv].secret);
	}
	free_str(&h->user);
	free_str(&h->port);
	free_str(&h->rem_addr);
	free_str(&h->data);
	free_str(&h->user_msg);
	for (i=0; i<MAXAVPAIRS; i++)
		free_str(&(h->avs[i]));

	/* Clear everything else before freeing memory */
	memset(h, 0, sizeof(struct tac_handle));
	free(h);
}

int
tac_config(struct tac_handle *h, const char *path)
{
	FILE *fp;
	char buf[MAXCONFLINE];
	int linenum;
	int retval;

	if (path == NULL)
		path = PATH_TACPLUS_CONF;
	if ((fp = fopen(path, "r")) == NULL) {
		generr(h, "Cannot open \"%s\": %s", path, strerror(errno));
		return -1;
	}
	retval = 0;
	linenum = 0;
	while (fgets(buf, sizeof buf, fp) != NULL) {
		int len;
		char *fields[4];
		int nfields;
		char msg[ERRSIZE];
		char *host, *res;
		char *port_str;
		char *secret;
		char *timeout_str;
		char *options_str;
		char *end;
		unsigned long timeout;
		int port;
		int options;

		linenum++;
		len = strlen(buf);
		/* We know len > 0, else fgets would have returned NULL. */
		if (buf[len - 1] != '\n') {
			if (len >= sizeof buf - 1)
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
		nfields = split(buf, fields, 4, msg, sizeof msg);
		if (nfields == -1) {
			generr(h, "%s:%d: %s", path, linenum, msg);
			retval = -1;
			break;
		}
		if (nfields == 0)
			continue;
		if (nfields < 2) {
			generr(h, "%s:%d: missing shared secret", path,
			    linenum);
			retval = -1;
			break;
		}
		host = fields[0];
		secret = fields[1];
		timeout_str = fields[2];
		options_str = fields[3];

		/* Parse and validate the fields. */
		res = host;
		host = strsep(&res, ":");
		port_str = strsep(&res, ":");
		if (port_str != NULL) {
			port = strtoul(port_str, &end, 10);
			if (port_str[0] == '\0' || *end != '\0') {
				generr(h, "%s:%d: invalid port", path,
				    linenum);
				retval = -1;
				break;
			}
		} else
			port = 0;
		if (timeout_str != NULL) {
			timeout = strtoul(timeout_str, &end, 10);
			if (timeout_str[0] == '\0' || *end != '\0') {
				generr(h, "%s:%d: invalid timeout", path,
				    linenum);
				retval = -1;
				break;
			}
		} else
			timeout = TIMEOUT;
		options = 0;
		if (options_str != NULL) {
			if (strcmp(options_str, "single-connection") == 0)
				options |= TAC_SRVR_SINGLE_CONNECT;
			else {
				generr(h, "%s:%d: invalid option \"%s\"",
				    path, linenum, options_str);
				retval = -1;
				break;
			}
		};

		if (tac_add_server(h, host, port, secret, timeout,
		    options) == -1) {
			char msg[ERRSIZE];

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

int
tac_create_authen(struct tac_handle *h, int action, int type, int service)
{
	struct tac_authen_start *as;

	create_msg(h, TAC_AUTHEN, action, type);

	as = &h->request.u.authen_start;
	as->action = action;
	as->priv_lvl = TAC_PRIV_LVL_USER;
	as->authen_type = type;
	as->service = service;

	return 0;
}

int
tac_create_author(struct tac_handle *h, int method, int type, int service)
{
	struct tac_author_request *areq;

	create_msg(h, TAC_AUTHOR, method, type);

	areq = &h->request.u.author_request;
	areq->authen_meth = method;
	areq->priv_lvl = TAC_PRIV_LVL_USER;
	areq->authen_type = type;
	areq->service = service;

	return 0;
}

int
tac_create_acct(struct tac_handle *h, int acct, int action, int type, int service)
{
	struct tac_acct_start *as;

	create_msg(h, TAC_ACCT, action, type);

	as = &h->request.u.acct_start;
	as->action = acct;
	as->authen_action = action;
	as->priv_lvl = TAC_PRIV_LVL_USER;
	as->authen_type = type;
	as->authen_service = service;

	return 0;
}

static void
create_msg(struct tac_handle *h, int msg_type, int var, int type)
{
	struct tac_msg *msg;
	int i;

	h->last_seq_no = 0;

	msg = &h->request;
	msg->type = msg_type;
	msg->version = protocol_version(msg_type, var, type);
	msg->flags = 0; /* encrypted packet body */

	free_str(&h->user);
	free_str(&h->port);
	free_str(&h->rem_addr);
	free_str(&h->data);
	free_str(&h->user_msg);

	for (i=0; i<MAXAVPAIRS; i++)
		free_str(&(h->avs[i]));
}

void *
tac_get_data(struct tac_handle *h, size_t *len)
{
	return dup_str(h, &h->srvr_data, len);
}

char *
tac_get_msg(struct tac_handle *h)
{
	return dup_str(h, &h->srvr_msg, NULL);
}

/*
 * Create and initialize a tac_handle structure, and return it to the
 * caller.  Can fail only if the necessary memory cannot be allocated.
 * In that case, it returns NULL.
 */
struct tac_handle *
tac_open(void)
{
	int i;
	struct tac_handle *h;

	h = (struct tac_handle *)malloc(sizeof(struct tac_handle));
	if (h != NULL) {
		h->fd = -1;
		h->num_servers = 0;
		h->cur_server = 0;
		h->errmsg[0] = '\0';
		init_clnt_str(&h->user);
		init_clnt_str(&h->port);
		init_clnt_str(&h->rem_addr);
		init_clnt_str(&h->data);
		init_clnt_str(&h->user_msg);
		for (i=0; i<MAXAVPAIRS; i++) {
			init_clnt_str(&(h->avs[i]));
			init_srvr_str(&(h->srvr_avs[i]));
		}
		init_srvr_str(&h->srvr_msg);
		init_srvr_str(&h->srvr_data);
		srandomdev();
	}
	return h;
}

int
tac_send_authen(struct tac_handle *h)
{
	struct tac_authen_reply *ar;

	if (h->num_servers == 0)
	    return -1;

	if (h->last_seq_no == 0) {	/* Authentication START packet */
		struct tac_authen_start *as;

		as = &h->request.u.authen_start;
		h->request.length =
		    htonl(offsetof(struct tac_authen_start, rest[0]));
		if (add_str_8(h, &as->user_len, &h->user) == -1 ||
		    add_str_8(h, &as->port_len, &h->port) == -1 ||
		    add_str_8(h, &as->rem_addr_len, &h->rem_addr) == -1 ||
		    add_str_8(h, &as->data_len, &h->data) == -1)
			return -1;
	} else {			/* Authentication CONTINUE packet */
		struct tac_authen_cont *ac;

		ac = &h->request.u.authen_cont;
		ac->flags = 0;
		h->request.length =
		    htonl(offsetof(struct tac_authen_cont, rest[0]));
		if (add_str_16(h, &ac->user_msg_len, &h->user_msg) == -1 ||
		    add_str_16(h, &ac->data_len, &h->data) == -1)
			return -1;
	}

	/* Send the message and retrieve the reply. */
	if (send_msg(h) == -1 || recv_msg(h) == -1)
		return -1;

	/* Scan the optional fields in the reply. */
	ar = &h->response.u.authen_reply;
	h->srvr_pos = offsetof(struct tac_authen_reply, rest[0]);
	if (get_srvr_str(h, "msg", &h->srvr_msg, ntohs(ar->msg_len)) == -1 ||
	    get_srvr_str(h, "data", &h->srvr_data, ntohs(ar->data_len)) == -1 ||
	    get_srvr_end(h) == -1)
		return -1;

	if (!h->single_connect &&
	    ar->status != TAC_AUTHEN_STATUS_GETDATA &&
	    ar->status != TAC_AUTHEN_STATUS_GETUSER &&
	    ar->status != TAC_AUTHEN_STATUS_GETPASS)
		close_connection(h);

	return ar->flags << 8 | ar->status;
}

int
tac_send_author(struct tac_handle *h)
{
	int i, current;
	char dbgstr[64];
	struct tac_author_request *areq = &h->request.u.author_request;
	struct tac_author_response *ares = &h->response.u.author_response;

	h->request.length =
		htonl(offsetof(struct tac_author_request, rest[0]));

	/* Count each specified AV pair */
	for (areq->av_cnt=0, i=0; i<MAXAVPAIRS; i++)
		if (h->avs[i].len && h->avs[i].data)
			areq->av_cnt++;

	/*
	 * Each AV size is a byte starting right after 'av_cnt'.  Update the
	 * offset to include these AV sizes.
	 */
	h->request.length = ntohl(htonl(h->request.length) + areq->av_cnt);

	/* Now add the string arguments from 'h' */
	if (add_str_8(h, &areq->user_len, &h->user) == -1 ||
	    add_str_8(h, &areq->port_len, &h->port) == -1 ||
	    add_str_8(h, &areq->rem_addr_len, &h->rem_addr) == -1)
		return -1;

	/* Add each AV pair, the size of each placed in areq->rest[current] */
	for (current=0, i=0; i<MAXAVPAIRS; i++) {
		if (h->avs[i].len && h->avs[i].data) {
			if (add_str_8(h, &areq->rest[current++],
				      &(h->avs[i])) == -1)
				return -1;
		}
	}

	/* Send the message and retrieve the reply. */
	if (send_msg(h) == -1 || recv_msg(h) == -1)
		return -1;

	/* Update the offset in the response packet based on av pairs count */
	h->srvr_pos = offsetof(struct tac_author_response, rest[0]) +
		ares->av_cnt;

	/* Scan the optional fields in the response. */
	if (get_srvr_str(h, "msg", &h->srvr_msg, ntohs(ares->msg_len)) == -1 ||
	    get_srvr_str(h, "data", &h->srvr_data, ntohs(ares->data_len)) ==-1)
		return -1;

	/* Get each AV pair (just setting pointers, not malloc'ing) */
	clear_srvr_avs(h);
	for (i=0; i<ares->av_cnt; i++) {
		snprintf(dbgstr, sizeof dbgstr, "av-pair-%d", i);
		if (get_srvr_str(h, dbgstr, &(h->srvr_avs[i]),
				 ares->rest[i]) == -1)
			return -1;
	}

	/* Should have ended up at the end */
	if (get_srvr_end(h) == -1)
		return -1;

	/* Sanity checks */
	if (!h->single_connect)
		close_connection(h);

	return ares->av_cnt << 8 | ares->status;
}

int
tac_send_acct(struct tac_handle *h)
{
	register int i, current;
	struct tac_acct_start *as = &h->request.u.acct_start;
	struct tac_acct_reply *ar = &h->response.u.acct_reply;

	/* start */
	as = &h->request.u.acct_start;
	h->request.length = htonl(offsetof(struct tac_acct_start, rest[0]));
	for (as->av_cnt = 0, i = 0; i < MAXAVPAIRS; i++)
		if (h->avs[i].len && h->avs[i].data)
			as->av_cnt++;
	h->request.length = ntohl(htonl(h->request.length) + as->av_cnt);

	if (add_str_8(h, &as->user_len, &h->user) == -1 ||
	    add_str_8(h, &as->port_len, &h->port) == -1 ||
	    add_str_8(h, &as->rem_addr_len, &h->rem_addr) == -1)
		return -1;

	for (i = current = 0; i < MAXAVPAIRS; i++)
		if (h->avs[i].len && h->avs[i].data)
			if (add_str_8(h, &as->rest[current++], &(h->avs[i])) == -1)
				return -1;

	/* send */
	if (send_msg(h) == -1 || recv_msg(h) == -1)
		return -1;

	/* reply */
	h->srvr_pos = offsetof(struct tac_acct_reply, rest[0]);
	if (get_srvr_str(h, "msg", &h->srvr_msg, ntohs(ar->msg_len)) == -1 ||
	    get_srvr_str(h, "data", &h->srvr_data, ntohs(ar->data_len)) == -1 ||
	    get_srvr_end(h) == -1)
		return -1;

	/* Sanity checks */
	if (!h->single_connect)
		close_connection(h);

	return ar->status;
}

int
tac_set_rem_addr(struct tac_handle *h, const char *addr)
{
	return save_str(h, &h->rem_addr, addr, addr != NULL ? strlen(addr) : 0);
}

int
tac_set_data(struct tac_handle *h, const void *data, size_t data_len)
{
	return save_str(h, &h->data, data, data_len);
}

int
tac_set_msg(struct tac_handle *h, const char *msg)
{
	return save_str(h, &h->user_msg, msg, msg != NULL ? strlen(msg) : 0);
}

int
tac_set_port(struct tac_handle *h, const char *port)
{
	return save_str(h, &h->port, port, port != NULL ? strlen(port) : 0);
}

int
tac_set_priv(struct tac_handle *h, int priv)
{
	if (!(TAC_PRIV_LVL_MIN <= priv && priv <= TAC_PRIV_LVL_MAX)) {
		generr(h, "Attempt to set invalid privilege level");
		return -1;
	}
	h->request.u.authen_start.priv_lvl = priv;
	return 0;
}

int
tac_set_user(struct tac_handle *h, const char *user)
{
	return save_str(h, &h->user, user, user != NULL ? strlen(user) : 0);
}

int
tac_set_av(struct tac_handle *h, u_int index, const char *av)
{
	if (index >= MAXAVPAIRS)
		return -1;
	return save_str(h, &(h->avs[index]), av, av != NULL ? strlen(av) : 0);
}

char *
tac_get_av(struct tac_handle *h, u_int index)
{
	if (index >= MAXAVPAIRS)
		return NULL;
	return dup_str(h, &(h->srvr_avs[index]), NULL);
}

char *
tac_get_av_value(struct tac_handle *h, const char *attribute)
{
	int i, len;
	const char *ch, *end;
	const char *candidate;
	int   candidate_len;
	int   found_seperator;
	struct srvr_str srvr;

	if (attribute == NULL || ((len = strlen(attribute)) == 0))
		return NULL;

	for (i=0; i<MAXAVPAIRS; i++) {
		candidate = h->srvr_avs[i].data;
		candidate_len = h->srvr_avs[i].len;

		/*
		 * Valid 'srvr_avs' guaranteed to be contiguous starting at 
		 * index 0 (not necessarily the case with 'avs').  Break out
		 * when the "end" of the list has been reached.
		 */
		if (!candidate)
			break;

		if (len < candidate_len && 
		    !strncmp(candidate, attribute, len)) {

			ch = candidate + len;
			end = candidate + candidate_len;

			/*
			 * Sift out the white space between A and V (should not
			 * be any, but don't trust implementation of server...)
			 */
			found_seperator = 0;
			while ((*ch == '=' || *ch == '*' || *ch == ' ' ||
				*ch == '\t') && ch != end) {
				if (*ch == '=' || *ch == '*')
					found_seperator++;
				ch++;
			}

			/*
			 * Note:
			 *     The case of 'attribute' == "foo" and
			 *     h->srvr_avs[0] = "foobie=var1"
			 *     h->srvr_avs[1] = "foo=var2"
			 * is handled.
			 *
			 * Note that for empty string attribute values a
			 * 0-length string is returned in order to distinguish
			 * against unset values.
			 * dup_str() will handle srvr.len == 0 correctly.
			 */
			if (found_seperator == 1) {
				srvr.len = end - ch;
				srvr.data = ch;
				return dup_str(h, &srvr, NULL);
			}
		}
	}
	return NULL;
}

void
tac_clear_avs(struct tac_handle *h)
{
	int i;
	for (i=0; i<MAXAVPAIRS; i++)
		save_str(h, &(h->avs[i]), NULL, 0);
}

static void
clear_srvr_avs(struct tac_handle *h)
{
	int i;
	for (i=0; i<MAXAVPAIRS; i++)
		init_srvr_str(&(h->srvr_avs[i]));
}


const char *
tac_strerror(struct tac_handle *h)
{
	return h->errmsg;
}

static void *
xmalloc(struct tac_handle *h, size_t size)
{
	void *r;

	if ((r = malloc(size)) == NULL)
		generr(h, "Out of memory");
	return r;
}

static char *
xstrdup(struct tac_handle *h, const char *s)
{
	char *r;

	if ((r = strdup(s)) == NULL)
		generr(h, "Out of memory");
	return r;
}
