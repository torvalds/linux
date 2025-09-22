/*	$OpenBSD: server_fcgi.c,v 1.97 2023/11/08 19:19:10 millert Exp $	*/

/*
 * Copyright (c) 2014 Florian Obser <florian@openbsd.org>
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

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include <event.h>
#include <unistd.h>

#include "httpd.h"
#include "http.h"

#define FCGI_PADDING_SIZE	 255
#define FCGI_RECORD_SIZE	 \
    (sizeof(struct fcgi_record_header) + FCGI_CONTENT_SIZE + FCGI_PADDING_SIZE)

#define FCGI_BEGIN_REQUEST	 1
#define FCGI_ABORT_REQUEST	 2
#define FCGI_END_REQUEST	 3
#define FCGI_PARAMS		 4
#define FCGI_STDIN		 5
#define FCGI_STDOUT		 6
#define FCGI_STDERR		 7
#define FCGI_DATA		 8
#define FCGI_GET_VALUES		 9
#define FCGI_GET_VALUES_RESULT	10
#define FCGI_UNKNOWN_TYPE	11
#define FCGI_MAXTYPE		(FCGI_UNKNOWN_TYPE)

#define FCGI_RESPONDER		 1

struct fcgi_record_header {
	uint8_t		version;
	uint8_t		type;
	uint16_t	id;
	uint16_t	content_len;
	uint8_t		padding_len;
	uint8_t		reserved;
} __packed;

struct fcgi_begin_request_body {
	uint16_t	role;
	uint8_t		flags;
	uint8_t		reserved[5];
} __packed;

struct server_fcgi_param {
	int		total_len;
	uint8_t		buf[FCGI_RECORD_SIZE];
};

int	server_fcgi_header(struct client *, unsigned int);
void	server_fcgi_error(struct bufferevent *, short, void *);
void	server_fcgi_read(struct bufferevent *, void *);
int	server_fcgi_writeheader(struct client *, struct kv *, void *);
int	server_fcgi_writechunk(struct client *);
int	server_fcgi_getheaders(struct client *);
int	fcgi_add_param(struct server_fcgi_param *, const char *, const char *,
	    struct client *);

int
server_fcgi(struct httpd *env, struct client *clt)
{
	struct server_fcgi_param	 param;
	struct server_config		*srv_conf = clt->clt_srv_conf;
	struct http_descriptor		*desc = clt->clt_descreq;
	struct fcgi_record_header	*h;
	struct fcgi_begin_request_body	*begin;
	struct fastcgi_param		*fcgiparam;
	char				 hbuf[HOST_NAME_MAX+1];
	size_t				 scriptlen;
	int				 pathlen;
	int				 fd = -1, ret;
	const char			*stripped, *alias, *errstr = NULL;
	char				*query_alias, *str, *script = NULL;

	if ((fd = socket(srv_conf->fastcgi_ss.ss_family,
	    SOCK_STREAM | SOCK_NONBLOCK, 0)) == -1)
		goto fail;
	if ((connect(fd, (struct sockaddr *) &srv_conf->fastcgi_ss,
	    srv_conf->fastcgi_ss.ss_len)) == -1) {
		if (errno != EINPROGRESS)
			goto fail;
	}

	memset(hbuf, 0, sizeof(hbuf));
	clt->clt_fcgi.state = FCGI_READ_HEADER;
	clt->clt_fcgi.toread = sizeof(struct fcgi_record_header);
	clt->clt_fcgi.status = 200;
	clt->clt_fcgi.headersdone = 0;
	clt->clt_fcgi.headerssent = 0;

	if (clt->clt_srvevb != NULL)
		evbuffer_free(clt->clt_srvevb);

	clt->clt_srvevb = evbuffer_new();
	if (clt->clt_srvevb == NULL) {
		errstr = "failed to allocate evbuffer";
		goto fail;
	}

	close(clt->clt_fd);
	clt->clt_fd = fd;

	if (clt->clt_srvbev != NULL)
		bufferevent_free(clt->clt_srvbev);

	clt->clt_srvbev_throttled = 0;
	clt->clt_srvbev = bufferevent_new(fd, server_fcgi_read,
	    NULL, server_fcgi_error, clt);
	if (clt->clt_srvbev == NULL) {
		errstr = "failed to allocate fcgi buffer event";
		goto fail;
	}

	memset(&param, 0, sizeof(param));

	h = (struct fcgi_record_header *)&param.buf;
	h->version = 1;
	h->type = FCGI_BEGIN_REQUEST;
	h->id = htons(1);
	h->content_len = htons(sizeof(struct fcgi_begin_request_body));
	h->padding_len = 0;

	begin = (struct fcgi_begin_request_body *)&param.buf[sizeof(struct
	    fcgi_record_header)];
	begin->role = htons(FCGI_RESPONDER);

	if (bufferevent_write(clt->clt_srvbev, &param.buf,
	    sizeof(struct fcgi_record_header) +
	    sizeof(struct fcgi_begin_request_body)) == -1) {
		errstr = "failed to write to evbuffer";
		goto fail;
	}

	h->type = FCGI_PARAMS;
	h->content_len = param.total_len = 0;

	alias = desc->http_path_alias != NULL
	    ? desc->http_path_alias
	    : desc->http_path;

	query_alias = desc->http_query_alias != NULL
	    ? desc->http_query_alias
	    : desc->http_query;

	stripped = server_root_strip(alias, srv_conf->strip);
	if ((pathlen = asprintf(&script, "%s%s", srv_conf->root, stripped))
	    == -1) {
		errstr = "failed to get script name";
		goto fail;
	}

	scriptlen = path_info(script);
	/*
	 * no part of root should show up in PATH_INFO.
	 * therefore scriptlen should be >= strlen(root)
	 */
	if (scriptlen < strlen(srv_conf->root))
		scriptlen = strlen(srv_conf->root);
	if ((int)scriptlen < pathlen) {
		if (fcgi_add_param(&param, "PATH_INFO",
		    script + scriptlen, clt) == -1) {
			errstr = "failed to encode param";
			goto fail;
		}
		script[scriptlen] = '\0';
	} else {
		/* RFC 3875 mandates that PATH_INFO is empty if not set */
		if (fcgi_add_param(&param, "PATH_INFO", "", clt) == -1) {
			errstr = "failed to encode param";
			goto fail;
		}
	}

	/*
	 * calculate length of http SCRIPT_NAME:
	 * add length of stripped prefix,
	 * subtract length of prepended local root
	 */
	scriptlen += (stripped - alias) - strlen(srv_conf->root);
	if ((str = strndup(alias, scriptlen)) == NULL)
		goto fail;
	ret = fcgi_add_param(&param, "SCRIPT_NAME", str, clt);
	free(str);
	if (ret == -1) {
		errstr = "failed to encode param";
		goto fail;
	}
	if (fcgi_add_param(&param, "SCRIPT_FILENAME", server_root_strip(script,
	    srv_conf->fcgistrip), clt) == -1) {
		errstr = "failed to encode param";
		goto fail;
	}

	if (query_alias) {
		if (fcgi_add_param(&param, "QUERY_STRING", query_alias,
		    clt) == -1) {
			errstr = "failed to encode param";
			goto fail;
		}
	} else if (fcgi_add_param(&param, "QUERY_STRING", "", clt) == -1) {
		errstr = "failed to encode param";
		goto fail;
	}

	if (fcgi_add_param(&param, "DOCUMENT_ROOT", server_root_strip(
	    srv_conf->root, srv_conf->fcgistrip), clt) == -1) {
		errstr = "failed to encode param";
		goto fail;
	}
	if (fcgi_add_param(&param, "DOCUMENT_URI", alias,
	    clt) == -1) {
		errstr = "failed to encode param";
		goto fail;
	}
	if (fcgi_add_param(&param, "GATEWAY_INTERFACE", "CGI/1.1",
	    clt) == -1) {
		errstr = "failed to encode param";
		goto fail;
	}

	if (srv_conf->flags & SRVFLAG_AUTH) {
		if (fcgi_add_param(&param, "REMOTE_USER",
		    clt->clt_remote_user, clt) == -1) {
			errstr = "failed to encode param";
			goto fail;
		}
	}

	/* Add HTTP_* headers */
	if (server_headers(clt, desc, server_fcgi_writeheader, &param) == -1) {
		errstr = "failed to encode param";
		goto fail;
	}

	if (srv_conf->flags & SRVFLAG_TLS) {
		if (fcgi_add_param(&param, "HTTPS", "on", clt) == -1) {
			errstr = "failed to encode param";
			goto fail;
		}
		if (srv_conf->tls_flags != 0 && fcgi_add_param(&param,
		    "TLS_PEER_VERIFY", printb_flags(srv_conf->tls_flags,
		    TLSFLAG_BITS), clt) == -1) {
			errstr = "failed to encode param";
			goto fail;
		}
	}

	TAILQ_FOREACH(fcgiparam, &srv_conf->fcgiparams, entry) {
		if (fcgi_add_param(&param, fcgiparam->name, fcgiparam->value,
		    clt) == -1) {
			errstr = "failed to encode param";
			goto fail;
		}
	}

	(void)print_host(&clt->clt_ss, hbuf, sizeof(hbuf));
	if (fcgi_add_param(&param, "REMOTE_ADDR", hbuf, clt) == -1) {
		errstr = "failed to encode param";
		goto fail;
	}

	(void)snprintf(hbuf, sizeof(hbuf), "%d", ntohs(clt->clt_port));
	if (fcgi_add_param(&param, "REMOTE_PORT", hbuf, clt) == -1) {
		errstr = "failed to encode param";
		goto fail;
	}

	if (fcgi_add_param(&param, "REQUEST_METHOD",
	    server_httpmethod_byid(desc->http_method), clt) == -1) {
		errstr = "failed to encode param";
		goto fail;
	}

	if (!desc->http_query) {
		if (fcgi_add_param(&param, "REQUEST_URI", desc->http_path_orig,
		    clt) == -1) {
			errstr = "failed to encode param";
			goto fail;
		}
	} else {
		if (asprintf(&str, "%s?%s", desc->http_path_orig,
		    desc->http_query) == -1) {
			errstr = "failed to encode param";
			goto fail;
		}
		ret = fcgi_add_param(&param, "REQUEST_URI", str, clt);
		free(str);
		if (ret == -1) {
			errstr = "failed to encode param";
			goto fail;
		}
	}

	(void)print_host(&clt->clt_srv_ss, hbuf, sizeof(hbuf));
	if (fcgi_add_param(&param, "SERVER_ADDR", hbuf, clt) == -1) {
		errstr = "failed to encode param";
		goto fail;
	}

	(void)snprintf(hbuf, sizeof(hbuf), "%d",
	    ntohs(server_socket_getport(&clt->clt_srv_ss)));
	if (fcgi_add_param(&param, "SERVER_PORT", hbuf, clt) == -1) {
		errstr = "failed to encode param";
		goto fail;
	}

	if (fcgi_add_param(&param, "SERVER_NAME", srv_conf->name,
	    clt) == -1) {
		errstr = "failed to encode param";
		goto fail;
	}

	if (fcgi_add_param(&param, "SERVER_PROTOCOL", desc->http_version,
	    clt) == -1) {
		errstr = "failed to encode param";
		goto fail;
	}

	if (fcgi_add_param(&param, "SERVER_SOFTWARE", HTTPD_SERVERNAME,
	    clt) == -1) {
		errstr = "failed to encode param";
		goto fail;
	}

	if (param.total_len != 0) {	/* send last params record */
		if (bufferevent_write(clt->clt_srvbev, &param.buf,
		    sizeof(struct fcgi_record_header) +
		    ntohs(h->content_len)) == -1) {
			errstr = "failed to write to client evbuffer";
			goto fail;
		}
	}

	/* send "no more params" message */
	h->content_len = 0;
	if (bufferevent_write(clt->clt_srvbev, &param.buf,
	    sizeof(struct fcgi_record_header)) == -1) {
		errstr = "failed to write to client evbuffer";
		goto fail;
	}

	bufferevent_settimeout(clt->clt_srvbev,
	    srv_conf->timeout.tv_sec, srv_conf->timeout.tv_sec);
	bufferevent_enable(clt->clt_srvbev, EV_READ|EV_WRITE);
	if (clt->clt_toread != 0) {
		/*
		 * XXX - Work around UAF: server_read_httpcontent() can call
		 * server_close(), normally freeing clt. If clt->clt_fcgi_count
		 * reaches 0, call server_close() via server_abort_http().
		 */
		clt->clt_fcgi_count++;
		server_read_httpcontent(clt->clt_bev, clt);
		if (clt->clt_fcgi_count-- <= 0) {
			errstr = clt->clt_fcgi_error;
			goto fail;
		}
		bufferevent_enable(clt->clt_bev, EV_READ);
	} else {
		bufferevent_disable(clt->clt_bev, EV_READ);
		fcgi_add_stdin(clt, NULL);
	}

	if (strcmp(desc->http_version, "HTTP/1.1") == 0) {
		clt->clt_fcgi.chunked = 1;
	} else {
		/* HTTP/1.0 does not support chunked encoding */
		clt->clt_fcgi.chunked = 0;
		clt->clt_persist = 0;
	}
	clt->clt_fcgi.end = 0;
	clt->clt_done = 0;

	free(script);
	return (0);
 fail:
	free(script);
	if (errstr == NULL)
		errstr = strerror(errno);
	if (fd != -1 && clt->clt_fd != fd)
		close(fd);
	server_abort_http(clt, 500, errstr);
	return (-1);
}

int
fcgi_add_stdin(struct client *clt, struct evbuffer *evbuf)
{
	struct fcgi_record_header	h;

	memset(&h, 0, sizeof(h));
	h.version = 1;
	h.type = FCGI_STDIN;
	h.id = htons(1);
	h.padding_len = 0;

	if (evbuf == NULL) {
		h.content_len = 0;
		return bufferevent_write(clt->clt_srvbev, &h,
		    sizeof(struct fcgi_record_header));
	} else {
		h.content_len = htons(EVBUFFER_LENGTH(evbuf));
		if (bufferevent_write(clt->clt_srvbev, &h,
		    sizeof(struct fcgi_record_header)) == -1)
			return -1;
		return bufferevent_write_buffer(clt->clt_srvbev, evbuf);
	}
	return (0);
}

int
fcgi_add_param(struct server_fcgi_param *p, const char *key,
    const char *val, struct client *clt)
{
	struct fcgi_record_header	*h;
	int				 len = 0;
	int				 key_len = strlen(key);
	int				 val_len = strlen(val);
	uint8_t				*param;

	len += key_len + val_len;
	len += key_len > 127 ? 4 : 1;
	len += val_len > 127 ? 4 : 1;

	DPRINTF("%s: %s[%d] => %s[%d], total_len: %d", __func__, key, key_len,
	    val, val_len, p->total_len);

	if (len > FCGI_CONTENT_SIZE)
		return (-1);

	if (p->total_len + len > FCGI_CONTENT_SIZE) {
		if (bufferevent_write(clt->clt_srvbev, p->buf,
		    sizeof(struct fcgi_record_header) + p->total_len) == -1)
			return (-1);
		p->total_len = 0;
	}

	h = (struct fcgi_record_header *)p->buf;
	param = p->buf + sizeof(*h) + p->total_len;

	if (key_len > 127) {
		*param++ = ((key_len >> 24) & 0xff) | 0x80;
		*param++ = ((key_len >> 16) & 0xff);
		*param++ = ((key_len >> 8) & 0xff);
		*param++ = (key_len & 0xff);
	} else
		*param++ = key_len;

	if (val_len > 127) {
		*param++ = ((val_len >> 24) & 0xff) | 0x80;
		*param++ = ((val_len >> 16) & 0xff);
		*param++ = ((val_len >> 8) & 0xff);
		*param++ = (val_len & 0xff);
	} else
		*param++ = val_len;

	memcpy(param, key, key_len);
	param += key_len;
	memcpy(param, val, val_len);

	p->total_len += len;

	h->content_len = htons(p->total_len);
	return (0);
}

void
server_fcgi_error(struct bufferevent *bev, short error, void *arg)
{
	struct client		*clt = arg;
	struct http_descriptor	*desc = clt->clt_descreq;

	if ((error & EVBUFFER_EOF) && !clt->clt_fcgi.headersdone) {
		server_abort_http(clt, 500, "malformed or no headers");
		return;
	}

	/* send the end marker if not already */
	if (desc->http_method != HTTP_METHOD_HEAD && clt->clt_fcgi.chunked &&
	    !clt->clt_fcgi.end++)
		server_bufferevent_print(clt, "0\r\n\r\n");

	server_file_error(bev, error, arg);
}

void
server_fcgi_read(struct bufferevent *bev, void *arg)
{
	uint8_t				 buf[FCGI_RECORD_SIZE];
	struct client			*clt = (struct client *) arg;
	struct fcgi_record_header	*h;
	size_t				 len;
	char				*ptr;

	do {
		len = bufferevent_read(bev, buf, clt->clt_fcgi.toread);
		if (evbuffer_add(clt->clt_srvevb, buf, len) == -1) {
			server_abort_http(clt, 500, "short write");
			return;
		}
		clt->clt_fcgi.toread -= len;
		DPRINTF("%s: len: %lu toread: %d state: %d type: %d",
		    __func__, len, clt->clt_fcgi.toread,
		    clt->clt_fcgi.state, clt->clt_fcgi.type);

		if (clt->clt_fcgi.toread != 0)
			return;

		switch (clt->clt_fcgi.state) {
		case FCGI_READ_HEADER:
			clt->clt_fcgi.state = FCGI_READ_CONTENT;
			h = (struct fcgi_record_header *)
			    EVBUFFER_DATA(clt->clt_srvevb);
			DPRINTF("%s: record header: version %d type %d id %d "
			    "content len %d padding %d", __func__,
			    h->version, h->type, ntohs(h->id),
			    ntohs(h->content_len), h->padding_len);
			clt->clt_fcgi.type = h->type;
			clt->clt_fcgi.toread = ntohs(h->content_len);
			clt->clt_fcgi.padding_len = h->padding_len;
			evbuffer_drain(clt->clt_srvevb,
			    EVBUFFER_LENGTH(clt->clt_srvevb));
			if (clt->clt_fcgi.toread != 0)
				break;
			else if (clt->clt_fcgi.type == FCGI_STDOUT &&
			    !clt->clt_chunk) {
				server_abort_http(clt, 500, "empty stdout");
				return;
			}

			/* fallthrough if content_len == 0 */
		case FCGI_READ_CONTENT:
			switch (clt->clt_fcgi.type) {
			case FCGI_STDERR:
				if (EVBUFFER_LENGTH(clt->clt_srvevb) > 0 &&
				    (ptr = get_string(
				    EVBUFFER_DATA(clt->clt_srvevb),
				    EVBUFFER_LENGTH(clt->clt_srvevb)))
				    != NULL) {
					server_sendlog(clt->clt_srv_conf,
					    IMSG_LOG_ERROR, "%s", ptr);
					free(ptr);
				}
				break;
			case FCGI_STDOUT:
				++clt->clt_chunk;
				if (!clt->clt_fcgi.headersdone) {
					clt->clt_fcgi.headersdone =
					    server_fcgi_getheaders(clt);
					if (!EVBUFFER_LENGTH(clt->clt_srvevb))
						break;
				}
				/* FALLTHROUGH */
			case FCGI_END_REQUEST:
				if (clt->clt_fcgi.headersdone &&
				    !clt->clt_fcgi.headerssent) {
					if (server_fcgi_header(clt,
					    clt->clt_fcgi.status) == -1) {
						server_abort_http(clt, 500,
						    "malformed fcgi headers");
						return;
					}
				}
				/* Don't send content for HEAD requests */
				if (clt->clt_fcgi.headerssent &&
				    clt->clt_descreq->http_method
				    == HTTP_METHOD_HEAD)
					/* nothing */ ;
				else if (server_fcgi_writechunk(clt) == -1) {
					server_abort_http(clt, 500,
					    "encoding error");
					return;
				}
				if (clt->clt_fcgi.type == FCGI_END_REQUEST) {
					bufferevent_enable(clt->clt_bev,
					    EV_READ|EV_WRITE);
					if (clt->clt_persist)
						clt->clt_toread =
						    TOREAD_HTTP_HEADER;
					else
						clt->clt_toread =
						    TOREAD_HTTP_NONE;
					clt->clt_done = 0;
					server_reset_http(clt);
				}
				break;
			}
			evbuffer_drain(clt->clt_srvevb,
			    EVBUFFER_LENGTH(clt->clt_srvevb));
			if (!clt->clt_fcgi.padding_len) {
				clt->clt_fcgi.state = FCGI_READ_HEADER;
				clt->clt_fcgi.toread =
				    sizeof(struct fcgi_record_header);
			} else {
				clt->clt_fcgi.state = FCGI_READ_PADDING;
				clt->clt_fcgi.toread =
				    clt->clt_fcgi.padding_len;
			}
			break;
		case FCGI_READ_PADDING:
			evbuffer_drain(clt->clt_srvevb,
			    EVBUFFER_LENGTH(clt->clt_srvevb));
			clt->clt_fcgi.state = FCGI_READ_HEADER;
			clt->clt_fcgi.toread =
			    sizeof(struct fcgi_record_header);
			break;
		}
	} while (len > 0);
}

int
server_fcgi_header(struct client *clt, unsigned int code)
{
	struct server_config	*srv_conf = clt->clt_srv_conf;
	struct http_descriptor	*desc = clt->clt_descreq;
	struct http_descriptor	*resp = clt->clt_descresp;
	const char		*error;
	char			 tmbuf[32];
	struct kv		*kv, *cl, key;

	clt->clt_fcgi.headerssent = 1;

	if (desc == NULL || (error = server_httperror_byid(code)) == NULL)
		return (-1);

	if (server_log_http(clt, code, 0) == -1)
		return (-1);

	/* Add error codes */
	if (kv_setkey(&resp->http_pathquery, "%u", code) == -1 ||
	    kv_set(&resp->http_pathquery, "%s", error) == -1)
		return (-1);

	/* Add headers */
	if (kv_add(&resp->http_headers, "Server", HTTPD_SERVERNAME) == NULL)
		return (-1);

	if (clt->clt_fcgi.type == FCGI_END_REQUEST ||
	    EVBUFFER_LENGTH(clt->clt_srvevb) == 0) {
		/* Can't chunk encode an empty body. */
		clt->clt_fcgi.chunked = 0;

		/* But then we need a Content-Length unless method is HEAD... */
		if (desc->http_method != HTTP_METHOD_HEAD) {
			key.kv_key = "Content-Length";
			if ((kv = kv_find(&resp->http_headers, &key)) == NULL) {
				if (kv_add(&resp->http_headers,
				    "Content-Length", "0") == NULL)
					return (-1);
			}
		}
	}

	/* Send chunked encoding header */
	if (clt->clt_fcgi.chunked) {
		/* but only if no Content-Length header is supplied */
		key.kv_key = "Content-Length";
		if ((kv = kv_find(&resp->http_headers, &key)) != NULL) {
			clt->clt_fcgi.chunked = 0;
		} else {
			/*
			 * XXX What if the FastCGI added some kind of
			 * Transfer-Encoding, like gzip, deflate or even
			 * "chunked"?
			 */
			if (kv_add(&resp->http_headers,
			    "Transfer-Encoding", "chunked") == NULL)
				return (-1);
		}
	}

	/* Is it a persistent connection? */
	if (clt->clt_persist) {
		if (kv_add(&resp->http_headers,
		    "Connection", "keep-alive") == NULL)
			return (-1);
	} else if (kv_add(&resp->http_headers, "Connection", "close") == NULL)
		return (-1);

	/* HSTS header */
	if (srv_conf->flags & SRVFLAG_SERVER_HSTS &&
	    srv_conf->flags & SRVFLAG_TLS) {
		if ((cl =
		    kv_add(&resp->http_headers, "Strict-Transport-Security",
		    NULL)) == NULL ||
		    kv_set(cl, "max-age=%d%s%s", srv_conf->hsts_max_age,
		    srv_conf->hsts_flags & HSTSFLAG_SUBDOMAINS ?
		    "; includeSubDomains" : "",
		    srv_conf->hsts_flags & HSTSFLAG_PRELOAD ?
		    "; preload" : "") == -1)
			return (-1);
	}

	/* Date header is mandatory and should be added as late as possible */
	key.kv_key = "Date";
	if (kv_find(&resp->http_headers, &key) == NULL &&
	    (server_http_time(time(NULL), tmbuf, sizeof(tmbuf)) <= 0 ||
	    kv_add(&resp->http_headers, "Date", tmbuf) == NULL))
		return (-1);

	if (server_writeresponse_http(clt) == -1 ||
	    server_bufferevent_print(clt, "\r\n") == -1 ||
	    server_headers(clt, resp, server_writeheader_http, NULL) == -1 ||
	    server_bufferevent_print(clt, "\r\n") == -1)
		return (-1);

	return (0);
}

int
server_fcgi_writeheader(struct client *clt, struct kv *hdr, void *arg)
{
	struct server_fcgi_param	*param = arg;
	char				*val, *name, *p;
	const char			*key;
	int				 ret;

	/* The key might have been updated in the parent */
	if (hdr->kv_parent != NULL && hdr->kv_parent->kv_key != NULL)
		key = hdr->kv_parent->kv_key;
	else
		key = hdr->kv_key;

	val = hdr->kv_value;

	if (strcasecmp(key, "Content-Length") == 0 ||
	    strcasecmp(key, "Content-Type") == 0) {
		if ((name = strdup(key)) == NULL)
			return (-1);
	} else {
		if (asprintf(&name, "HTTP_%s", key) == -1)
			return (-1);
	}

	/*
	 * RFC 7230 defines a header field-name as a "token" and a "token"
	 * is defined as one or more characters for which isalpha or
	 * isdigit is true plus a list of additional characters.
	 * According to RFC 3875 a CGI environment variable is created
	 * by converting all letters to upper case and replacing '-'
	 * with '_'.
	 */
	for (p = name; *p != '\0'; p++) {
		if (isalpha((unsigned char)*p))
			*p = toupper((unsigned char)*p);
		else if (!(*p == '!' || *p == '#' || *p == '$' || *p == '%' ||
		    *p == '&' || *p == '\'' || *p == '*' || *p == '+' ||
		    *p == '.' || *p == '^' || *p == '_' || *p == '`' ||
		    *p == '|' || *p == '~' || isdigit((unsigned char)*p)))
			*p = '_';
	}

	ret = fcgi_add_param(param, name, val, clt);
	free(name);

	return (ret);
}

int
server_fcgi_writechunk(struct client *clt)
{
	struct evbuffer *evb = clt->clt_srvevb;
	size_t		 len;

	if (clt->clt_fcgi.type == FCGI_END_REQUEST) {
		len = 0;
	} else
		len = EVBUFFER_LENGTH(evb);

	if (clt->clt_fcgi.chunked) {
		/* If len is 0, make sure to write the end marker only once */
		if (len == 0 && clt->clt_fcgi.end++)
			return (0);
		if (server_bufferevent_printf(clt, "%zx\r\n", len) == -1 ||
		    server_bufferevent_write_chunk(clt, evb, len) == -1 ||
		    server_bufferevent_print(clt, "\r\n") == -1)
			return (-1);
	} else if (len)
		return (server_bufferevent_write_buffer(clt, evb));

	return (0);
}

int
server_fcgi_getheaders(struct client *clt)
{
	struct http_descriptor	*resp = clt->clt_descresp;
	struct evbuffer		*evb = clt->clt_srvevb;
	int			 code, ret;
	char			*line, *key, *value;
	const char		*errstr;

	while ((line = evbuffer_getline(evb)) != NULL && *line != '\0') {
		key = line;

		if ((value = strchr(key, ':')) == NULL)
			break;

		*value++ = '\0';
		value += strspn(value, " \t");

		DPRINTF("%s: %s: %s", __func__, key, value);

		if (strcasecmp("Status", key) == 0) {
			value[strcspn(value, " \t")] = '\0';
			code = (int)strtonum(value, 100, 600, &errstr);
			if (errstr != NULL || server_httperror_byid(
			    code) == NULL)
				code = 200;
			clt->clt_fcgi.status = code;
		} else {
			(void)kv_add(&resp->http_headers, key, value);
		}
		free(line);
	}

	ret = (line != NULL && *line == '\0');

	free(line);
	return ret;
}
