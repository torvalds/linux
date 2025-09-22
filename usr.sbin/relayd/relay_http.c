/*	$OpenBSD: relay_http.c,v 1.90 2024/07/20 06:54:15 anton Exp $	*/

/*
 * Copyright (c) 2006 - 2016 Reyk Floeter <reyk@openbsd.org>
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
#include <sys/queue.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/tree.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <event.h>
#include <fnmatch.h>
#include <siphash.h>
#include <imsg.h>
#include <unistd.h>

#include "relayd.h"
#include "http.h"

static int	_relay_lookup_url(struct ctl_relay_event *, char *, char *,
		    char *, struct kv *);
int		 relay_lookup_url(struct ctl_relay_event *,
		    const char *, struct kv *);
int		 relay_lookup_query(struct ctl_relay_event *, struct kv *);
int		 relay_lookup_cookie(struct ctl_relay_event *, const char *,
		    struct kv *);
void		 relay_read_httpcontent(struct bufferevent *, void *);
void		 relay_read_httpchunks(struct bufferevent *, void *);
char		*relay_expand_http(struct ctl_relay_event *, char *,
		    char *, size_t);
int		 relay_writeheader_kv(struct ctl_relay_event *, struct kv *);
int		 relay_writeheader_http(struct ctl_relay_event *,
		    struct ctl_relay_event *);
int		 relay_writerequest_http(struct ctl_relay_event *,
		    struct ctl_relay_event *);
int		 relay_writeresponse_http(struct ctl_relay_event *,
		    struct ctl_relay_event *);
void		 relay_reset_http(struct ctl_relay_event *);
static int	 relay_httpmethod_cmp(const void *, const void *);
static int	 relay_httperror_cmp(const void *, const void *);
int		 relay_httpquery_test(struct ctl_relay_event *,
		    struct relay_rule *, struct kvlist *);
int		 relay_httpheader_test(struct ctl_relay_event *,
		    struct relay_rule *, struct kvlist *);
int		 relay_httppath_test(struct ctl_relay_event *,
		    struct relay_rule *, struct kvlist *);
int		 relay_httpurl_test(struct ctl_relay_event *,
		    struct relay_rule *, struct kvlist *);
int		 relay_httpcookie_test(struct ctl_relay_event *,
		    struct relay_rule *, struct kvlist *);
int		 relay_apply_actions(struct ctl_relay_event *, struct kvlist *,
		    struct relay_table *);
int		 relay_match_actions(struct ctl_relay_event *,
		    struct relay_rule *, struct kvlist *, struct kvlist *,
		    struct relay_table **);
void		 relay_httpdesc_free(struct http_descriptor *);
char *		 server_root_strip(char *, int);

static struct relayd	*env = NULL;

static struct http_method	 http_methods[] = HTTP_METHODS;
static struct http_error	 http_errors[] = HTTP_ERRORS;

void
relay_http(struct relayd *x_env)
{
	if (x_env != NULL)
		env = x_env;

	DPRINTF("%s: sorting lookup tables, pid %d", __func__, getpid());

	/* Sort the HTTP lookup arrays */
	qsort(http_methods, sizeof(http_methods) /
	    sizeof(http_methods[0]) - 1,
	    sizeof(http_methods[0]), relay_httpmethod_cmp);
	qsort(http_errors, sizeof(http_errors) /
	    sizeof(http_errors[0]) - 1,
	    sizeof(http_errors[0]), relay_httperror_cmp);
}

void
relay_http_init(struct relay *rlay)
{
	rlay->rl_proto->close = relay_close_http;

	relay_http(NULL);

	/* Calculate skip step for the filter rules (may take a while) */
	relay_calc_skip_steps(&rlay->rl_proto->rules);
}

int
relay_http_priv_init(struct rsession *con)
{

	struct http_session	*hs;

	if ((hs = calloc(1, sizeof(*hs))) == NULL)
		return (-1);
	SIMPLEQ_INIT(&hs->hs_methods);
	DPRINTF("%s: session %d http_session %p", __func__,
		con->se_id, hs);
	con->se_priv = hs;
	return (relay_httpdesc_init(&con->se_in));
}

int
relay_httpdesc_init(struct ctl_relay_event *cre)
{
	struct http_descriptor	*desc;

	if ((desc = calloc(1, sizeof(*desc))) == NULL)
		return (-1);

	RB_INIT(&desc->http_headers);
	cre->desc = desc;

	return (0);
}

void
relay_httpdesc_free(struct http_descriptor *desc)
{
	if (desc == NULL)
		return;

	free(desc->http_path);
	desc->http_path = NULL;
	free(desc->http_query);
	desc->http_query = NULL;
	free(desc->http_version);
	desc->http_version = NULL;
	free(desc->query_key);
	desc->query_key = NULL;
	free(desc->query_val);
	desc->query_val = NULL;
	kv_purge(&desc->http_headers);
	desc->http_lastheader = NULL;
}

static int
relay_http_header_name_valid(const char *name)
{
	/*
	 * RFC 9110 specifies that only the following characters are
	 * permitted within HTTP header field names.
	 */
	const char token_chars[] = "!#$%&'*+-.^_`|~0123456789"
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
	const size_t len = strspn(name, token_chars);

	return (name[len] == '\0');
}

void
relay_read_http(struct bufferevent *bev, void *arg)
{
	struct ctl_relay_event	*cre = arg;
	struct http_descriptor	*desc = cre->desc;
	struct rsession		*con = cre->con;
	struct relay		*rlay = con->se_relay;
	struct protocol		*proto = rlay->rl_proto;
	struct evbuffer		*src = EVBUFFER_INPUT(bev);
	char			*line = NULL, *key, *value;
	char			*urlproto, *host, *path;
	int			 action, unique, ret;
	const char		*errstr;
	size_t			 size, linelen;
	struct kv		*hdr = NULL;
	struct kv		*upgrade = NULL, *upgrade_ws = NULL;
	struct kv		*connection_close = NULL;
	int			 ws_response = 0;
	struct http_method_node	*hmn;
	struct http_session	*hs;
	enum httpmethod		 request_method;

	getmonotime(&con->se_tv_last);
	cre->timedout = 0;

	size = EVBUFFER_LENGTH(src);
	DPRINTF("%s: session %d: size %lu, to read %lld",
	    __func__, con->se_id, size, cre->toread);
	if (size == 0) {
		if (cre->dir == RELAY_DIR_RESPONSE)
			return;
		cre->toread = TOREAD_HTTP_HEADER;
		goto done;
	}

	for (;;) {
		line = evbuffer_readln(src, &linelen, EVBUFFER_EOL_CRLF);
		if (line == NULL) {
			/*
			 * We do not process the last header on premature
			 * EOF as it may not be complete.
			 */
			break;
		}

		/*
		 * An empty line indicates the end of the request.
		 * libevent already stripped the \r\n for us.
		 */
		if (linelen == 0) {
			cre->done = 1;
			free(line);
			line = NULL;
			if (cre->line > 1) {
				/* Process last (complete) header line. */
				goto last_header;
			}
			break;
		}

		/* Limit the total header length minus \r\n */
		cre->headerlen += linelen;
		if (cre->headerlen > proto->httpheaderlen) {
			relay_abort_http(con, 413,
			    "request headers too large", 0);
			goto abort;
		}

		/* Reject requests with an embedded NUL byte. */
		if (memchr(line, '\0', linelen) != NULL) {
			relay_abort_http(con, 400, "malformed", 0);
			goto abort;
		}

		hs = con->se_priv;
		DPRINTF("%s: session %d http_session %p", __func__,
			con->se_id, hs);

		/*
		 * The first line is the GET/POST/PUT/... request,
		 * subsequent lines are HTTP headers.
		 */
		if (++cre->line == 1) {
			key = line;
			if ((value = strchr(key, ' ')) == NULL) {
				relay_abort_http(con, 400, "malformed", 0);
				goto abort;
			}
			*value++ = '\0';

			if (cre->dir == RELAY_DIR_RESPONSE) {
				desc->http_method = HTTP_METHOD_RESPONSE;
				hmn = SIMPLEQ_FIRST(&hs->hs_methods);

				/*
				 * There is nothing preventing the relay from
				 * sending an unbalanced response.  Be prepared.
				 */
				if (hmn == NULL) {
					request_method = HTTP_METHOD_NONE;
					DPRINTF("%s: session %d unbalanced "
					    "response", __func__, con->se_id);
				} else {
					SIMPLEQ_REMOVE_HEAD(&hs->hs_methods,
					    hmn_entry);
					request_method = hmn->hmn_method;
					DPRINTF("%s: session %d dequeuing %s",
					    __func__, con->se_id,
					    relay_httpmethod_byid(request_method));
					free(hmn);
				}

				/*
				 * Decode response path and query
				 */
				desc->http_version = strdup(key);
				if (desc->http_version == NULL) {
					free(line);
					goto fail;
				}
				desc->http_rescode = strdup(value);
				if (desc->http_rescode == NULL) {
					free(line);
					goto fail;
				}
				desc->http_resmesg = strchr(desc->http_rescode,
				    ' ');
				if (desc->http_resmesg == NULL) {
					free(line);
					goto fail;
				}
				*desc->http_resmesg++ = '\0';
				desc->http_resmesg = strdup(desc->http_resmesg);
				if (desc->http_resmesg == NULL) {
					free(line);
					goto fail;
				}
				desc->http_status = strtonum(desc->http_rescode,
				    100, 599, &errstr);
				if (errstr) {
					DPRINTF(
					    "%s: http_status %s: errno %d, %s",
					    __func__, desc->http_rescode, errno,
					    errstr);
					free(line);
					goto fail;
				}
				DPRINTF("http_version %s http_rescode %s "
				    "http_resmesg %s", desc->http_version,
				    desc->http_rescode, desc->http_resmesg);
			} else if (cre->dir == RELAY_DIR_REQUEST) {
				desc->http_method =
				    relay_httpmethod_byname(key);
				if (desc->http_method == HTTP_METHOD_NONE) {
					free(line);
					goto fail;
				}
				if ((hmn = calloc(1, sizeof *hmn)) == NULL) {
					free(line);
					goto fail;
				}
				hmn->hmn_method = desc->http_method;
				DPRINTF("%s: session %d enqueuing %s",
				    __func__, con->se_id,
				    relay_httpmethod_byid(hmn->hmn_method));
				SIMPLEQ_INSERT_TAIL(&hs->hs_methods, hmn,
				    hmn_entry);
				/*
				 * Decode request path and query
				 */
				desc->http_path = strdup(value);
				if (desc->http_path == NULL) {
					free(line);
					goto fail;
				}
				desc->http_version = strchr(desc->http_path,
				    ' ');
				if (desc->http_version == NULL) {
					free(line);
					goto fail;
				}
				*desc->http_version++ = '\0';
				desc->http_query = strchr(desc->http_path, '?');
				if (desc->http_query != NULL)
					*desc->http_query++ = '\0';

				/*
				 * Have to allocate the strings because they
				 * could be changed independently by the
				 * filters later.
				 */
				if ((desc->http_version =
				    strdup(desc->http_version)) == NULL) {
					free(line);
					goto fail;
				}
				if (desc->http_query != NULL &&
				    (desc->http_query =
				    strdup(desc->http_query)) == NULL) {
					free(line);
					goto fail;
				}
			}

			free(line);
			continue;
		}

		/* Multiline headers wrap with a space or tab. */
		if (*line == ' ' || *line == '\t') {
			if (cre->line == 2) {
				/* First header line cannot start with space. */
				relay_abort_http(con, 400, "malformed", 0);
				goto abort;
			}

			/* Append line to the last header, if present */
			if (kv_extend(&desc->http_headers,
			    desc->http_lastheader, line) == NULL) {
				free(line);
				goto fail;
			}

			free(line);
			continue;
		}

		/* Process the last complete header line. */
 last_header:
		if (desc->http_lastheader != NULL) {
			key = desc->http_lastheader->kv_key;
			value = desc->http_lastheader->kv_value;

			DPRINTF("%s: session %d: header '%s: %s'", __func__,
			    con->se_id, key, value);

			if (desc->http_method != HTTP_METHOD_NONE &&
			    strcasecmp("Content-Length", key) == 0) {
				switch (desc->http_method) {
				case HTTP_METHOD_TRACE:
				case HTTP_METHOD_CONNECT:
					/*
					 * These methods should not have a body
					 * and thus no Content-Length header.
					 */
					relay_abort_http(con, 400, "malformed",
					    0);
					goto abort;
				case HTTP_METHOD_GET:
				case HTTP_METHOD_HEAD:
				case HTTP_METHOD_COPY:
				case HTTP_METHOD_MOVE:
					/*
					 * We strip the body (if present) from
					 * the GET, HEAD, COPY and MOVE methods
					 * so strip Content-Length too.
					 */
					kv_delete(&desc->http_headers,
					    desc->http_lastheader);
					break;
				case HTTP_METHOD_RESPONSE:
					if (request_method == HTTP_METHOD_HEAD)
						break;
					/* FALLTHROUGH */
				default:
					/*
					 * Need to read data from the client
					 * after the HTTP header.
					 * XXX What about non-standard clients
					 * not using the carriage return? And
					 * some browsers seem to include the
					 * line length in the content-length.
					 */
					if (*value == '+' || *value == '-') {
						errstr = "invalid";
					} else {
						cre->toread = strtonum(value, 0,
						    LLONG_MAX, &errstr);
					}
					if (errstr) {
						relay_abort_http(con, 500,
						    errstr, 0);
						goto abort;
					}
					break;
				}
				/*
				 * Response with a status code of 1xx
				 * (Informational) or 204 (No Content) MUST
				 * not have a Content-Length (rfc 7230 3.3.3)
				 * Instead we check for value != 0 because there
				 * are servers that do not follow the rfc and
				 * send Content-Length: 0.
				 */
				if (desc->http_method == HTTP_METHOD_RESPONSE &&
				    (((desc->http_status >= 100 &&
				    desc->http_status < 200) ||
				    desc->http_status == 204)) &&
				    cre->toread != 0) {
					relay_abort_http(con, 502,
					    "Bad Gateway", 0);
					goto abort;
				}
			}
			if (strcasecmp("Transfer-Encoding", key) == 0) {
				/* We don't support other encodings. */
				if (strcasecmp("chunked", value) != 0) {
					relay_abort_http(con, 400,
					    "malformed", 0);
					goto abort;
				}
				desc->http_chunked = 1;
			}

			if (strcasecmp("Host", key) == 0) {
				/*
				 * The path may contain a URL.  The host in the
				 * URL has to match the Host: value.
				 */
				if (parse_url(desc->http_path,
				    &urlproto, &host, &path) == 0) {
					ret = strcasecmp(host, value);
					free(urlproto);
					free(host);
					free(path);
					if (ret != 0) {
						relay_abort_http(con, 400,
						    "malformed host", 0);
						goto abort;
					}
				}
			}
		}

		if (cre->done)
			break;

		/* Validate header field name and check for missing value. */
		key = line;
		if ((value = strchr(line, ':')) == NULL) {
			relay_abort_http(con, 400, "malformed", 0);
			goto abort;
		}
		*value++ = '\0';
		value += strspn(value, " \t\r\n");

		if (!relay_http_header_name_valid(key)) {
			relay_abort_http(con, 400, "malformed", 0);
			goto abort;
		}

		/* The "Host" header must only occur once. */
		unique = strcasecmp("Host", key) == 0;

		if ((hdr = kv_add(&desc->http_headers, key,
		    value, unique)) == NULL) {
			relay_abort_http(con, 400, "malformed header", 0);
			goto abort;
		}
		desc->http_lastheader = hdr;

		free(line);
	}

	if (cre->done) {
		if (desc->http_method == HTTP_METHOD_NONE) {
			relay_abort_http(con, 406, "no method", 0);
			return;
		}

		action = relay_test(proto, cre);
		switch (action) {
		case RES_FAIL:
			relay_close(con, "filter rule failed", 1);
			return;
		case RES_BAD:
			relay_abort_http(con, 400, "Bad Request",
			    con->se_label);
			return;
		case RES_INTERNAL:
			relay_abort_http(con, 500, "Internal Server Error",
			    con->se_label);
			return;
		}
		if (action != RES_PASS) {
			relay_abort_http(con, 403, "Forbidden", con->se_label);
			return;
		}

		/*
		 * HTTP 101 Switching Protocols
		 */

		upgrade = kv_find_value(&desc->http_headers,
		    "Connection", "upgrade", ",");
		upgrade_ws = kv_find_value(&desc->http_headers,
		    "Upgrade", "websocket", ",");
		ws_response = 0;
		if (cre->dir == RELAY_DIR_REQUEST && upgrade_ws != NULL) {
			if ((proto->httpflags & HTTPFLAG_WEBSOCKETS) == 0) {
				relay_abort_http(con, 403,
				    "Websocket Forbidden", 0);
				return;
			} else if (upgrade == NULL) {
				relay_abort_http(con, 400,
				    "Bad Websocket Request", 0);
				return;
			} else if (desc->http_method != HTTP_METHOD_GET) {
				relay_abort_http(con, 405,
				    "Websocket Method Not Allowed", 0);
				return;
			}
		} else if (cre->dir == RELAY_DIR_RESPONSE &&
		    desc->http_status == 101) {
			if (upgrade_ws != NULL && upgrade != NULL &&
			    (proto->httpflags & HTTPFLAG_WEBSOCKETS)) {
				ws_response = 1;
				cre->dst->toread = TOREAD_UNLIMITED;
				cre->dst->bev->readcb = relay_read;
			} else {
				relay_abort_http(con, 502,
				    "Bad Websocket Gateway", 0);
				return;
			}
		}

		connection_close = kv_find_value(&desc->http_headers,
		    "Connection", "close", ",");

		switch (desc->http_method) {
		case HTTP_METHOD_CONNECT:
			/* Data stream */
			cre->toread = TOREAD_UNLIMITED;
			bev->readcb = relay_read;
			break;
		case HTTP_METHOD_GET:
		case HTTP_METHOD_HEAD:
		/* WebDAV methods */
		case HTTP_METHOD_COPY:
		case HTTP_METHOD_MOVE:
			cre->toread = 0;
			break;
		case HTTP_METHOD_DELETE:
		case HTTP_METHOD_OPTIONS:
		case HTTP_METHOD_POST:
		case HTTP_METHOD_PUT:
		case HTTP_METHOD_RESPONSE:
		/* WebDAV methods */
		case HTTP_METHOD_PROPFIND:
		case HTTP_METHOD_PROPPATCH:
		case HTTP_METHOD_MKCOL:
		case HTTP_METHOD_LOCK:
		case HTTP_METHOD_UNLOCK:
		case HTTP_METHOD_VERSION_CONTROL:
		case HTTP_METHOD_REPORT:
		case HTTP_METHOD_CHECKOUT:
		case HTTP_METHOD_CHECKIN:
		case HTTP_METHOD_UNCHECKOUT:
		case HTTP_METHOD_MKWORKSPACE:
		case HTTP_METHOD_UPDATE:
		case HTTP_METHOD_LABEL:
		case HTTP_METHOD_MERGE:
		case HTTP_METHOD_BASELINE_CONTROL:
		case HTTP_METHOD_MKACTIVITY:
		case HTTP_METHOD_ORDERPATCH:
		case HTTP_METHOD_ACL:
		case HTTP_METHOD_MKREDIRECTREF:
		case HTTP_METHOD_UPDATEREDIRECTREF:
		case HTTP_METHOD_SEARCH:
		case HTTP_METHOD_PATCH:
			/* HTTP request payload */
			if (cre->toread > 0) {
				bev->readcb = relay_read_httpcontent;
			}

			/* Single-pass HTTP body */
			if (cre->toread < 0) {
				cre->toread = TOREAD_UNLIMITED;
				bev->readcb = relay_read;
			}
			break;
		default:
			/* HTTP handler */
			cre->toread = TOREAD_HTTP_HEADER;
			bev->readcb = relay_read_http;
			break;
		}
		if (desc->http_chunked) {
			/* Chunked transfer encoding */
			cre->toread = TOREAD_HTTP_CHUNK_LENGTH;
			bev->readcb = relay_read_httpchunks;
		}

		/*
		 * Ask the server to close the connection after this request
		 * since we don't read any further request headers. Only add
		 * this header if it does not already exist or if this is a
		 * outbound websocket upgrade response.
		 */
		if (cre->toread == TOREAD_UNLIMITED &&
			connection_close == NULL && !ws_response)
			if (kv_add(&desc->http_headers, "Connection",
			    "close", 0) == NULL)
				goto fail;

		if (cre->dir == RELAY_DIR_REQUEST) {
			if (relay_writerequest_http(cre->dst, cre) == -1)
			    goto fail;
		} else {
			if (relay_writeresponse_http(cre->dst, cre) == -1)
			    goto fail;
		}
		if (relay_bufferevent_print(cre->dst, "\r\n") == -1 ||
		    relay_writeheader_http(cre->dst, cre) == -1 ||
		    relay_bufferevent_print(cre->dst, "\r\n") == -1)
			goto fail;

		relay_reset_http(cre);
 done:
		if (cre->dir == RELAY_DIR_REQUEST && cre->toread <= 0 &&
		    cre->dst->state != STATE_CONNECTED) {
			if (rlay->rl_conf.fwdmode == FWD_TRANS) {
				relay_bindanyreq(con, 0, IPPROTO_TCP);
				return;
			}
			if (relay_connect(con) == -1) {
				relay_abort_http(con, 502, "session failed", 0);
				return;
			}
		}
	}
	if (con->se_done) {
		relay_close(con, "last http read (done)", 0);
		return;
	}
	switch (relay_splice(cre)) {
	case -1:
		relay_close(con, strerror(errno), 1);
	case 1:
		return;
	case 0:
		break;
	}
	bufferevent_enable(bev, EV_READ);
	if (EVBUFFER_LENGTH(src) && bev->readcb != relay_read_http)
		bev->readcb(bev, arg);
	/* The callback readcb() might have freed the session. */
	return;
 fail:
	relay_abort_http(con, 500, strerror(errno), 0);
	return;
 abort:
	free(line);
}

void
relay_read_httpcontent(struct bufferevent *bev, void *arg)
{
	struct ctl_relay_event	*cre = arg;
	struct rsession		*con = cre->con;
	struct protocol		*proto = con->se_relay->rl_proto;

	struct evbuffer		*src = EVBUFFER_INPUT(bev);
	size_t			 size;

	getmonotime(&con->se_tv_last);
	cre->timedout = 0;

	size = EVBUFFER_LENGTH(src);
	DPRINTF("%s: session %d: size %lu, to read %lld", __func__,
	    con->se_id, size, cre->toread);
	if (!size)
		return;
	if (relay_spliceadjust(cre) == -1)
		goto fail;

	if (cre->toread > 0) {
		/* Read content data */
		if ((off_t)size > cre->toread) {
			size = cre->toread;
			if (relay_bufferevent_write_chunk(cre->dst, src, size)
			    == -1)
				goto fail;
			cre->toread = 0;
		} else {
			if (relay_bufferevent_write_buffer(cre->dst, src) == -1)
				goto fail;
			cre->toread -= size;
		}
		DPRINTF("%s: done, size %lu, to read %lld", __func__,
		    size, cre->toread);
	}
	if (cre->toread == 0) {
		cre->toread = TOREAD_HTTP_HEADER;
		bev->readcb = relay_read_http;
	}
	if (con->se_done)
		goto done;
	bufferevent_enable(bev, EV_READ);

	if (cre->dst->bev && EVBUFFER_LENGTH(EVBUFFER_OUTPUT(cre->dst->bev)) >
	    (size_t)RELAY_MAX_PREFETCH * proto->tcpbufsiz)
		bufferevent_disable(cre->bev, EV_READ);

	if (bev->readcb != relay_read_httpcontent)
		bev->readcb(bev, arg);
	/* The callback readcb() might have freed the session. */
	return;
 done:
	relay_close(con, "last http content read", 0);
	return;
 fail:
	relay_close(con, strerror(errno), 1);
}

void
relay_read_httpchunks(struct bufferevent *bev, void *arg)
{
	struct ctl_relay_event	*cre = arg;
	struct rsession		*con = cre->con;
	struct protocol		*proto = con->se_relay->rl_proto;
	struct evbuffer		*src = EVBUFFER_INPUT(bev);
	char			*line, *ep;
	long long		 llval;
	size_t			 size, linelen;

	getmonotime(&con->se_tv_last);
	cre->timedout = 0;

	size = EVBUFFER_LENGTH(src);
	DPRINTF("%s: session %d: size %lu, to read %lld", __func__,
	    con->se_id, size, cre->toread);
	if (!size)
		return;
	if (relay_spliceadjust(cre) == -1)
		goto fail;

	if (cre->toread > 0) {
		/* Read chunk data */
		if ((off_t)size > cre->toread) {
			size = cre->toread;
			if (relay_bufferevent_write_chunk(cre->dst, src, size)
			    == -1)
				goto fail;
			cre->toread = 0;
		} else {
			if (relay_bufferevent_write_buffer(cre->dst, src) == -1)
				goto fail;
			cre->toread -= size;
		}
		DPRINTF("%s: done, size %lu, to read %lld", __func__,
		    size, cre->toread);
	}
	switch (cre->toread) {
	case TOREAD_HTTP_CHUNK_LENGTH:
		line = evbuffer_readln(src, &linelen, EVBUFFER_EOL_CRLF);
		if (line == NULL) {
			/* Ignore empty line, continue */
			bufferevent_enable(bev, EV_READ);
			return;
		}
		if (linelen == 0) {
			free(line);
			goto next;
		}

		/*
		 * Read prepended chunk size in hex without leading +0[Xx].
		 * The returned signed value must not be negative.
		 */
		if (line[0] == '+' || line[0] == '-' ||
		    (line[0] == '0' && (line[1] == 'x' || line[1] == 'X'))) {
			/* Reject values like 0xdead and 0XBEEF or +FEED. */
			ep = line;
		} else {
			errno = 0;
			llval = strtoll(line, &ep, 16);
		}
		if (ep == line || *ep != '\0' || llval < 0 ||
		    (errno == ERANGE && llval == LLONG_MAX)) {
			free(line);
			relay_close(con, "invalid chunk size", 1);
			return;
		}

		if (relay_bufferevent_print(cre->dst, line) == -1 ||
		    relay_bufferevent_print(cre->dst, "\r\n") == -1) {
			free(line);
			goto fail;
		}
		free(line);

		if ((cre->toread = llval) == 0) {
			DPRINTF("%s: last chunk", __func__);
			cre->toread = TOREAD_HTTP_CHUNK_TRAILER;
		}
		break;
	case TOREAD_HTTP_CHUNK_TRAILER:
		/* Last chunk is 0 bytes followed by trailer and empty line */
		line = evbuffer_readln(src, &linelen, EVBUFFER_EOL_CRLF);
		if (line == NULL) {
			/* Ignore empty line, continue */
			bufferevent_enable(bev, EV_READ);
			return;
		}
		if (relay_bufferevent_print(cre->dst, line) == -1 ||
		    relay_bufferevent_print(cre->dst, "\r\n") == -1) {
			free(line);
			goto fail;
		}
		if (linelen == 0) {
			/* Switch to HTTP header mode */
			cre->toread = TOREAD_HTTP_HEADER;
			bev->readcb = relay_read_http;
		}
		free(line);
		break;
	case 0:
		/* Chunk is terminated by an empty newline */
		line = evbuffer_readln(src, &linelen, EVBUFFER_EOL_CRLF);
		free(line);
		if (relay_bufferevent_print(cre->dst, "\r\n") == -1)
			goto fail;
		cre->toread = TOREAD_HTTP_CHUNK_LENGTH;
		break;
	}

 next:
	if (con->se_done)
		goto done;
	bufferevent_enable(bev, EV_READ);

	if (cre->dst->bev && EVBUFFER_LENGTH(EVBUFFER_OUTPUT(cre->dst->bev)) >
	    (size_t)RELAY_MAX_PREFETCH * proto->tcpbufsiz)
		bufferevent_disable(cre->bev, EV_READ);

	if (EVBUFFER_LENGTH(src))
		bev->readcb(bev, arg);
	/* The callback readcb() might have freed the session. */
	return;

 done:
	relay_close(con, "last http chunk read (done)", 0);
	return;
 fail:
	relay_close(con, strerror(errno), 1);
}

void
relay_reset_http(struct ctl_relay_event *cre)
{
	struct http_descriptor	*desc = cre->desc;

	relay_httpdesc_free(desc);
	desc->http_method = 0;
	desc->http_chunked = 0;
	cre->headerlen = 0;
	cre->line = 0;
	cre->done = 0;
}

static int
_relay_lookup_url(struct ctl_relay_event *cre, char *host, char *path,
    char *query, struct kv *kv)
{
	struct rsession		*con = cre->con;
	char			*val, *md = NULL;
	int			 ret = RES_FAIL;
	const char		*str = NULL;

	if (asprintf(&val, "%s%s%s%s",
	    host, path,
	    query == NULL ? "" : "?",
	    query == NULL ? "" : query) == -1) {
		relay_abort_http(con, 500, "failed to allocate URL", 0);
		return (RES_FAIL);
	}

	switch (kv->kv_digest) {
	case DIGEST_SHA1:
	case DIGEST_MD5:
		if ((md = digeststr(kv->kv_digest,
		    val, strlen(val), NULL)) == NULL) {
			relay_abort_http(con, 500,
			    "failed to allocate digest", 0);
			goto fail;
		}
		str = md;
		break;
	case DIGEST_NONE:
		str = val;
		break;
	}

	DPRINTF("%s: session %d: %s, %s: %d", __func__, con->se_id,
	    str, kv->kv_key, strcasecmp(kv->kv_key, str));

	if (strcasecmp(kv->kv_key, str) == 0) {
		ret = RES_DROP;
		goto fail;
	}

	ret = RES_PASS;
 fail:
	free(md);
	free(val);
	return (ret);
}

int
relay_lookup_url(struct ctl_relay_event *cre, const char *host, struct kv *kv)
{
	struct http_descriptor	*desc = (struct http_descriptor *)cre->desc;
	int			 i, j, dots;
	char			*hi[RELAY_MAXLOOKUPLEVELS], *p, *pp, *c, ch;
	char			 ph[HOST_NAME_MAX+1];
	int			 ret;

	if (desc->http_path == NULL)
		return (RES_PASS);

	/*
	 * This is an URL lookup algorithm inspired by
	 * http://code.google.com/apis/safebrowsing/
	 *     developers_guide.html#PerformingLookups
	 */

	DPRINTF("%s: host '%s', path '%s', query '%s'",
	    __func__, host, desc->http_path,
	    desc->http_query == NULL ? "" : desc->http_query);

	if (canonicalize_host(host, ph, sizeof(ph)) == NULL) {
		return (RES_BAD);
	}

	bzero(hi, sizeof(hi));
	for (dots = -1, i = strlen(ph) - 1; i > 0; i--) {
		if (ph[i] == '.' && ++dots)
			hi[dots - 1] = &ph[i + 1];
		if (dots > (RELAY_MAXLOOKUPLEVELS - 2))
			break;
	}
	if (dots == -1)
		dots = 0;
	hi[dots] = ph;

	if ((pp = strdup(desc->http_path)) == NULL) {
		return (RES_INTERNAL);
	}
	for (i = (RELAY_MAXLOOKUPLEVELS - 1); i >= 0; i--) {
		if (hi[i] == NULL)
			continue;

		/* 1. complete path with query */
		if (desc->http_query != NULL)
			if ((ret = _relay_lookup_url(cre, hi[i],
			    pp, desc->http_query, kv)) != RES_PASS)
				goto done;

		/* 2. complete path without query */
		if ((ret = _relay_lookup_url(cre, hi[i],
		    pp, NULL, kv)) != RES_PASS)
			goto done;

		/* 3. traverse path */
		for (j = 0, p = strchr(pp, '/');
		    p != NULL; p = strchr(p, '/'), j++) {
			if (j > (RELAY_MAXLOOKUPLEVELS - 2) || *(++p) == '\0')
				break;
			c = &pp[p - pp];
			ch = *c;
			*c = '\0';
			if ((ret = _relay_lookup_url(cre, hi[i],
			    pp, NULL, kv)) != RES_PASS)
				goto done;
			*c = ch;
		}
	}

	ret = RES_PASS;
 done:
	free(pp);
	return (ret);
}

int
relay_lookup_cookie(struct ctl_relay_event *cre, const char *str,
    struct kv *kv)
{
	char			*val, *ptr, *key, *value;
	int			 ret;

	if ((val = strdup(str)) == NULL) {
		return (RES_INTERNAL);
	}

	for (ptr = val; ptr != NULL && strlen(ptr);) {
		if (*ptr == ' ')
			*ptr++ = '\0';
		key = ptr;
		if ((ptr = strchr(ptr, ';')) != NULL)
			*ptr++ = '\0';
		/*
		 * XXX We do not handle attributes
		 * ($Path, $Domain, or $Port)
		 */
		if (*key == '$')
			continue;

		if ((value =
		    strchr(key, '=')) == NULL ||
		    strlen(value) < 1)
			continue;
		*value++ = '\0';
		if (*value == '"')
			*value++ = '\0';
		if (value[strlen(value) - 1] == '"')
			value[strlen(value) - 1] = '\0';

		DPRINTF("%s: key %s = %s, %s = %s : %d",
		    __func__, key, value, kv->kv_key, kv->kv_value,
		    strcasecmp(kv->kv_key, key));

		if (strcasecmp(kv->kv_key, key) == 0 &&
		    ((kv->kv_value == NULL) ||
		    (fnmatch(kv->kv_value, value,
		    FNM_CASEFOLD) != FNM_NOMATCH))) {
			ret = RES_DROP;
			goto done;
		}
	}

	ret = RES_PASS;

 done:
	free(val);
	return (ret);
}

int
relay_lookup_query(struct ctl_relay_event *cre, struct kv *kv)
{
	struct http_descriptor	*desc = cre->desc;
	struct kv		*match = &desc->http_matchquery;
	char			*val, *ptr, *tmpkey = NULL, *tmpval = NULL;
	int			 ret = -1;

	if (desc->http_query == NULL)
		return (-1);
	if ((val = strdup(desc->http_query)) == NULL) {
		return (RES_INTERNAL);
	}

	ptr = val;
	while (ptr != NULL && strlen(ptr)) {
		tmpkey = ptr;
		if ((ptr = strchr(ptr, '&')) != NULL)
			*ptr++ = '\0';
		if ((tmpval = strchr(tmpkey, '=')) == NULL || strlen(tmpval)
		    < 1)
			continue;
		*tmpval++ = '\0';

		if (fnmatch(kv->kv_key, tmpkey, 0) != FNM_NOMATCH &&
		    (kv->kv_value == NULL || fnmatch(kv->kv_value, tmpval, 0)
		    != FNM_NOMATCH))
			break;
		else
			tmpkey = NULL;
	}

	if (tmpkey == NULL || tmpval == NULL)
		goto done;

	match->kv_key = strdup(tmpkey);
	if (match->kv_key == NULL)
		goto done;
	match->kv_value = strdup(tmpval);
	if (match->kv_key == NULL)
		goto done;
	ret = 0;

 done:
	free(val);
	return (ret);
}

ssize_t
relay_http_time(time_t t, char *tmbuf, size_t len)
{
	struct tm		 tm;

	/* New HTTP/1.1 RFC 7231 prefers IMF-fixdate from RFC 5322 */
	if (t == -1 || gmtime_r(&t, &tm) == NULL)
		return (-1);
	else
		return (strftime(tmbuf, len, "%a, %d %h %Y %T %Z", &tm));
}

void
relay_abort_http(struct rsession *con, u_int code, const char *msg,
    u_int16_t labelid)
{
	struct relay		*rlay = con->se_relay;
	struct bufferevent	*bev = con->se_in.bev;
	const char		*httperr = NULL, *text = "";
	char			*httpmsg, *body = NULL;
	char			 tmbuf[32], hbuf[128];
	const char		*style, *label = NULL;
	int			 bodylen;

	if ((httperr = relay_httperror_byid(code)) == NULL)
		httperr = "Unknown Error";

	if (labelid != 0)
		label = label_id2name(labelid);

	/* In some cases this function may be called from generic places */
	if (rlay->rl_proto->type != RELAY_PROTO_HTTP ||
	    (rlay->rl_proto->flags & F_RETURN) == 0) {
		relay_close(con, msg, 0);
		return;
	}

	if (bev == NULL)
		goto done;

	/* Some system information */
	if (print_host(&rlay->rl_conf.ss, hbuf, sizeof(hbuf)) == NULL)
		goto done;

	if (relay_http_time(time(NULL), tmbuf, sizeof(tmbuf)) <= 0)
		goto done;

	/* Do not send details of the Internal Server Error */
	switch (code) {
	case 500:
		break;
	default:
		text = msg;
		break;
	}

	/* A CSS stylesheet allows minimal customization by the user */
	style = (rlay->rl_proto->style != NULL) ? rlay->rl_proto->style :
	    "body { background-color: #a00000; color: white; font-family: "
	    "'Comic Sans MS', 'Chalkboard SE', 'Comic Neue', sans-serif; }\n"
	    "hr { border: 0; border-bottom: 1px dashed; }\n";

	/* Generate simple HTTP+HTML error document */
	if ((bodylen = asprintf(&body,
	    "<!DOCTYPE html>\n"
	    "<html>\n"
	    "<head>\n"
	    "<title>%03d %s</title>\n"
	    "<style type=\"text/css\"><!--\n%s\n--></style>\n"
	    "</head>\n"
	    "<body>\n"
	    "<h1>%s</h1>\n"
	    "<div id='m'>%s</div>\n"
	    "<div id='l'>%s</div>\n"
	    "<hr><address>%s at %s port %d</address>\n"
	    "</body>\n"
	    "</html>\n",
	    code, httperr, style, httperr, text,
	    label == NULL ? "" : label,
	    RELAYD_SERVERNAME, hbuf, ntohs(rlay->rl_conf.port))) == -1)
		goto done;

	/* Generate simple HTTP+HTML error document */
	if (asprintf(&httpmsg,
	    "HTTP/1.0 %03d %s\r\n"
	    "Date: %s\r\n"
	    "Server: %s\r\n"
	    "Connection: close\r\n"
	    "Content-Type: text/html\r\n"
	    "Content-Length: %d\r\n"
	    "\r\n"
	    "%s",
	    code, httperr, tmbuf, RELAYD_SERVERNAME, bodylen, body) == -1)
		goto done;

	/* Dump the message without checking for success */
	relay_dump(&con->se_in, httpmsg, strlen(httpmsg));
	free(httpmsg);

 done:
	free(body);
	if (asprintf(&httpmsg, "%s (%03d %s)", msg, code, httperr) == -1)
		relay_close(con, msg, 1);
	else {
		relay_close(con, httpmsg, 1);
		free(httpmsg);
	}
}

void
relay_close_http(struct rsession *con)
{
	struct http_session	*hs = con->se_priv;
	struct http_method_node	*hmn;

	DPRINTF("%s: session %d http_session %p", __func__,
		con->se_id, hs);
	if (hs != NULL)
		while (!SIMPLEQ_EMPTY(&hs->hs_methods)) {
			hmn = SIMPLEQ_FIRST(&hs->hs_methods);
			SIMPLEQ_REMOVE_HEAD(&hs->hs_methods, hmn_entry);
			DPRINTF("%s: session %d freeing %s", __func__,
			    con->se_id, relay_httpmethod_byid(hmn->hmn_method));
			free(hmn);
		}
	relay_httpdesc_free(con->se_in.desc);
	free(con->se_in.desc);
	relay_httpdesc_free(con->se_out.desc);
	free(con->se_out.desc);
}

char *
relay_expand_http(struct ctl_relay_event *cre, char *val, char *buf,
    size_t len)
{
	struct rsession		*con = cre->con;
	struct relay		*rlay = con->se_relay;
	struct http_descriptor	*desc = cre->desc;
	struct kv		*host, key;
	char			 ibuf[128];

	if (strlcpy(buf, val, len) >= len)
		return (NULL);

	if (strstr(val, "$HOST") != NULL) {
		key.kv_key = "Host";
		host = kv_find(&desc->http_headers, &key);
		if (host) {
			if (host->kv_value == NULL)
				return (NULL);
			snprintf(ibuf, sizeof(ibuf), "%s", host->kv_value);
		} else {
			if (print_host(&rlay->rl_conf.ss,
			    ibuf, sizeof(ibuf)) == NULL)
				return (NULL);
		}
		if (expand_string(buf, len, "$HOST", ibuf))
			return (NULL);
	}
	if (strstr(val, "$REMOTE_") != NULL) {
		if (strstr(val, "$REMOTE_ADDR") != NULL) {
			if (print_host(&cre->ss, ibuf, sizeof(ibuf)) == NULL)
				return (NULL);
			if (expand_string(buf, len,
			    "$REMOTE_ADDR", ibuf) != 0)
				return (NULL);
		}
		if (strstr(val, "$REMOTE_PORT") != NULL) {
			snprintf(ibuf, sizeof(ibuf), "%u", ntohs(cre->port));
			if (expand_string(buf, len,
			    "$REMOTE_PORT", ibuf) != 0)
				return (NULL);
		}
	}
	if (strstr(val, "$SERVER_") != NULL) {
		if (strstr(val, "$SERVER_ADDR") != NULL) {
			if (print_host(&rlay->rl_conf.ss,
			    ibuf, sizeof(ibuf)) == NULL)
				return (NULL);
			if (expand_string(buf, len,
			    "$SERVER_ADDR", ibuf) != 0)
				return (NULL);
		}
		if (strstr(val, "$SERVER_PORT") != NULL) {
			snprintf(ibuf, sizeof(ibuf), "%u",
			    ntohs(rlay->rl_conf.port));
			if (expand_string(buf, len,
			    "$SERVER_PORT", ibuf) != 0)
				return (NULL);
		}
		if (strstr(val, "$SERVER_NAME") != NULL) {
			if (expand_string(buf, len,
			    "$SERVER_NAME", RELAYD_SERVERNAME) != 0)
				return (NULL);
		}
	}
	if (strstr(val, "$TIMEOUT") != NULL) {
		snprintf(ibuf, sizeof(ibuf), "%lld",
		    (long long)rlay->rl_conf.timeout.tv_sec);
		if (expand_string(buf, len, "$TIMEOUT", ibuf) != 0)
			return (NULL);
	}

	return (buf);
}

int
relay_writerequest_http(struct ctl_relay_event *dst,
    struct ctl_relay_event *cre)
{
	struct http_descriptor	*desc = (struct http_descriptor *)cre->desc;
	const char		*name = NULL;

	if ((name = relay_httpmethod_byid(desc->http_method)) == NULL)
		return (-1);

	if (relay_bufferevent_print(dst, name) == -1 ||
	    relay_bufferevent_print(dst, " ") == -1 ||
	    relay_bufferevent_print(dst, desc->http_path) == -1 ||
	    (desc->http_query != NULL &&
	    (relay_bufferevent_print(dst, "?") == -1 ||
	    relay_bufferevent_print(dst, desc->http_query) == -1)) ||
	    relay_bufferevent_print(dst, " ") == -1 ||
	    relay_bufferevent_print(dst, desc->http_version) == -1)
		return (-1);

	return (0);
}

int
relay_writeresponse_http(struct ctl_relay_event *dst,
    struct ctl_relay_event *cre)
{
	struct http_descriptor	*desc = (struct http_descriptor *)cre->desc;

	DPRINTF("version: %s rescode: %s resmsg: %s", desc->http_version,
	    desc->http_rescode, desc->http_resmesg);

	if (relay_bufferevent_print(dst, desc->http_version) == -1 ||
	    relay_bufferevent_print(dst, " ") == -1 ||
	    relay_bufferevent_print(dst, desc->http_rescode) == -1 ||
	    relay_bufferevent_print(dst, " ") == -1 ||
	    relay_bufferevent_print(dst, desc->http_resmesg) == -1)
		return (-1);

	return (0);
}

int
relay_writeheader_kv(struct ctl_relay_event *dst, struct kv *hdr)
{
	char			*ptr;
	const char		*key;

	if (hdr->kv_flags & KV_FLAG_INVALID)
		return (0);

	/* The key might have been updated in the parent */
	if (hdr->kv_parent != NULL && hdr->kv_parent->kv_key != NULL)
		key = hdr->kv_parent->kv_key;
	else
		key = hdr->kv_key;

	ptr = hdr->kv_value;
	if (relay_bufferevent_print(dst, key) == -1 ||
	    (ptr != NULL &&
	    (relay_bufferevent_print(dst, ": ") == -1 ||
	    relay_bufferevent_print(dst, ptr) == -1 ||
	    relay_bufferevent_print(dst, "\r\n") == -1)))
		return (-1);
	DPRINTF("%s: %s: %s", __func__, key,
	    hdr->kv_value == NULL ? "" : hdr->kv_value);

	return (0);
}

int
relay_writeheader_http(struct ctl_relay_event *dst, struct ctl_relay_event
    *cre)
{
	struct kv		*hdr, *kv;
	struct http_descriptor	*desc = (struct http_descriptor *)cre->desc;

	RB_FOREACH(hdr, kvtree, &desc->http_headers) {
		if (relay_writeheader_kv(dst, hdr) == -1)
			return (-1);
		TAILQ_FOREACH(kv, &hdr->kv_children, kv_entry) {
			if (relay_writeheader_kv(dst, kv) == -1)
				return (-1);
		}
	}

	return (0);
}

enum httpmethod
relay_httpmethod_byname(const char *name)
{
	enum httpmethod		 id = HTTP_METHOD_NONE;
	struct http_method	 method, *res = NULL;

	/* Set up key */
	method.method_name = name;

	if ((res = bsearch(&method, http_methods,
	    sizeof(http_methods) / sizeof(http_methods[0]) - 1,
	    sizeof(http_methods[0]), relay_httpmethod_cmp)) != NULL)
		id = res->method_id;

	return (id);
}

const char *
relay_httpmethod_byid(u_int id)
{
	const char	*name = NULL;
	int		 i;

	for (i = 0; http_methods[i].method_name != NULL; i++) {
		if (http_methods[i].method_id == id) {
			name = http_methods[i].method_name;
			break;
		}
	}

	return (name);
}

static int
relay_httpmethod_cmp(const void *a, const void *b)
{
	const struct http_method *ma = a;
	const struct http_method *mb = b;

	/*
	 * RFC 2616 section 5.1.1 says that the method is case
	 * sensitive so we don't do a strcasecmp here.
	 */
	return (strcmp(ma->method_name, mb->method_name));
}

const char *
relay_httperror_byid(u_int id)
{
	struct http_error	 error, *res = NULL;

	/* Set up key */
	error.error_code = (int)id;

	res = bsearch(&error, http_errors,
	    sizeof(http_errors) / sizeof(http_errors[0]) - 1,
	    sizeof(http_errors[0]), relay_httperror_cmp);

	return (res->error_name);
}

static int
relay_httperror_cmp(const void *a, const void *b)
{
	const struct http_error *ea = a;
	const struct http_error *eb = b;
	return (ea->error_code - eb->error_code);
}

int
relay_httpquery_test(struct ctl_relay_event *cre, struct relay_rule *rule,
    struct kvlist *actions)
{
	struct http_descriptor	*desc = cre->desc;
	struct kv		*match = &desc->http_matchquery;
	struct kv		*kv = &rule->rule_kv[KEY_TYPE_QUERY];
	int			 res = 0;

	if (cre->dir == RELAY_DIR_RESPONSE || kv->kv_type != KEY_TYPE_QUERY)
		return (0);
	else if (kv->kv_key == NULL)
		return (0);
	else if ((res = relay_lookup_query(cre, kv)) != 0)
		return (res);

	relay_match(actions, kv, match, NULL);

	return (0);
}

int
relay_httpheader_test(struct ctl_relay_event *cre, struct relay_rule *rule,
    struct kvlist *actions)
{
	struct http_descriptor	*desc = cre->desc;
	struct kv		*kv = &rule->rule_kv[KEY_TYPE_HEADER];
	struct kv		*match;

	if (kv->kv_type != KEY_TYPE_HEADER)
		return (0);

	match = kv_find(&desc->http_headers, kv);

	if (kv->kv_option == KEY_OPTION_APPEND ||
	    kv->kv_option == KEY_OPTION_SET) {
		/* header can be NULL and will be added later */
	} else if (match == NULL) {
		/* Fail if header doesn't exist */
		return (-1);
	} else {
		if (fnmatch(kv->kv_key, match->kv_key,
		    FNM_CASEFOLD) == FNM_NOMATCH)
			return (-1);
		if (kv->kv_value != NULL &&
		    match->kv_value != NULL &&
		    fnmatch(kv->kv_value, match->kv_value, 0) == FNM_NOMATCH)
			return (-1);
	}

	relay_match(actions, kv, match, &desc->http_headers);

	return (0);
}

int
relay_httppath_test(struct ctl_relay_event *cre, struct relay_rule *rule,
    struct kvlist *actions)
{
	struct http_descriptor	*desc = cre->desc;
	struct kv		*kv = &rule->rule_kv[KEY_TYPE_PATH];
	struct kv		*match = &desc->http_pathquery;
	const char		*query;

	if (cre->dir == RELAY_DIR_RESPONSE || kv->kv_type != KEY_TYPE_PATH)
		return (0);
	else if (kv->kv_option != KEY_OPTION_STRIP) {
		if (kv->kv_key == NULL)
			return (0);
		else if (fnmatch(kv->kv_key, desc->http_path, 0) == FNM_NOMATCH)
			return (-1);
		else if (kv->kv_value != NULL && kv->kv_option == KEY_OPTION_NONE) {
			query = desc->http_query == NULL ? "" : desc->http_query;
			if (fnmatch(kv->kv_value, query, FNM_CASEFOLD) == FNM_NOMATCH)
				return (-1);
		}
	}

	relay_match(actions, kv, match, NULL);

	return (0);
}

int
relay_httpurl_test(struct ctl_relay_event *cre, struct relay_rule *rule,
    struct kvlist *actions)
{
	struct http_descriptor	*desc = cre->desc;
	struct kv		*host, key;
	struct kv		*kv = &rule->rule_kv[KEY_TYPE_URL];
	struct kv		*match = &desc->http_pathquery;
	int			 res;

	if (cre->dir == RELAY_DIR_RESPONSE || kv->kv_type != KEY_TYPE_URL ||
	    kv->kv_key == NULL)
		return (0);

	key.kv_key = "Host";
	host = kv_find(&desc->http_headers, &key);

	if (host == NULL || host->kv_value == NULL)
		return (0);
	else if (rule->rule_action != RULE_ACTION_BLOCK &&
	    kv->kv_option == KEY_OPTION_LOG &&
	    fnmatch(kv->kv_key, match->kv_key, FNM_CASEFOLD) != FNM_NOMATCH) {
		/* fnmatch url only for logging */
	} else if ((res = relay_lookup_url(cre, host->kv_value, kv)) != 0)
		return (res);
	relay_match(actions, kv, match, NULL);

	return (0);
}

int
relay_httpcookie_test(struct ctl_relay_event *cre, struct relay_rule *rule,
    struct kvlist *actions)
{
	struct http_descriptor	*desc = cre->desc;
	struct kv		*kv = &rule->rule_kv[KEY_TYPE_COOKIE], key;
	struct kv		*match = NULL;
	int			 res;

	if (kv->kv_type != KEY_TYPE_COOKIE)
		return (0);

	switch (cre->dir) {
	case RELAY_DIR_REQUEST:
		key.kv_key = "Cookie";
		break;
	case RELAY_DIR_RESPONSE:
		key.kv_key = "Set-Cookie";
		break;
	default:
		return (0);
		/* NOTREACHED */
		break;
	}

	if (kv->kv_option == KEY_OPTION_APPEND ||
	    kv->kv_option == KEY_OPTION_SET) {
		/* no cookie, can be NULL and will be added later */
	} else {
		match = kv_find(&desc->http_headers, &key);
		if (match == NULL)
			return (-1);
		if (kv->kv_key == NULL || match->kv_value == NULL)
			return (0);
		else if ((res = relay_lookup_cookie(cre, match->kv_value,
		    kv)) != 0)
			return (res);
	}

	relay_match(actions, kv, match, &desc->http_headers);

	return (0);
}

int
relay_match_actions(struct ctl_relay_event *cre, struct relay_rule *rule,
    struct kvlist *matches, struct kvlist *actions, struct relay_table **tbl)
{
	struct rsession		*con = cre->con;
	struct kv		*kv;

	/*
	 * Apply the following options instantly (action per match).
	 */
	if (rule->rule_table != NULL) {
		*tbl = rule->rule_table;
		con->se_out.ss.ss_family = AF_UNSPEC;
	}
	if (rule->rule_tag != 0)
		con->se_tag = rule->rule_tag == -1 ? 0 : rule->rule_tag;
	if (rule->rule_label != 0)
		con->se_label = rule->rule_label == -1 ? 0 : rule->rule_label;

	/*
	 * Apply the remaining options once after evaluation.
	 */
	if (matches == NULL) {
		/* 'pass' or 'block' rule */
		TAILQ_CONCAT(actions, &rule->rule_kvlist, kv_rule_entry);
	} else {
		/* 'match' rule */
		TAILQ_FOREACH(kv, matches, kv_match_entry) {
			TAILQ_INSERT_TAIL(actions, kv, kv_action_entry);
		}
	}

	return (0);
}

int
relay_apply_actions(struct ctl_relay_event *cre, struct kvlist *actions,
    struct relay_table *tbl)
{
	struct rsession		*con = cre->con;
	struct http_descriptor	*desc = cre->desc;
	struct kv		*host = NULL;
	const char		*value;
	struct kv		*kv, *match, *kp, *mp, kvcopy, matchcopy, key;
	int			 addkv, ret, nstrip;
	char			 buf[IBUF_READ_SIZE], *ptr;
	char			*msg = NULL;
	const char		*meth = NULL;

	memset(&kvcopy, 0, sizeof(kvcopy));
	memset(&matchcopy, 0, sizeof(matchcopy));

	ret = -1;
	kp = mp = NULL;
	TAILQ_FOREACH(kv, actions, kv_action_entry) {
		kp = NULL;
		match = kv->kv_match;
		addkv = 0;

		/*
		 * Although marked as deleted, give a chance to non-critical
		 * actions, ie. log, to be performed
		 */
		if (match != NULL && (match->kv_flags & KV_FLAG_INVALID))
			goto matchdel;

		switch (kv->kv_option) {
		case KEY_OPTION_APPEND:
		case KEY_OPTION_SET:
			switch (kv->kv_type) {
			case KEY_TYPE_PATH:
				if (kv->kv_option == KEY_OPTION_APPEND) {
					if (kv_setkey(match, "%s%s",
					    match->kv_key, kv->kv_key) == -1)
						goto fail;
				} else {
					if (kv_setkey(match, "%s",
					    kv->kv_value) == -1)
						goto fail;
				}
				break;
			case KEY_TYPE_COOKIE:
				kp = &kvcopy;
				if (kv_inherit(kp, kv) == NULL)
					goto fail;
				if (kv_set(kp, "%s=%s;", kp->kv_key,
				    kp->kv_value) == -1)
					goto fail;
				if (kv_setkey(kp, "%s", cre->dir ==
				    RELAY_DIR_REQUEST ?
				    "Cookie" : "Set-Cookie") == -1)
					goto fail;
				/* FALLTHROUGH cookie is a header */
			case KEY_TYPE_HEADER:
				if (match == NULL) {
					addkv = 1;
					break;
				}
				if (match->kv_value == NULL ||
				    kv->kv_option == KEY_OPTION_SET) {
					if (kv_set(match, "%s",
					    kv->kv_value) == -1)
						goto fail;
				} else
					addkv = 1;
				break;
			default:
				/* query, url not supported */
				break;
			}
			break;
		case KEY_OPTION_REMOVE:
			switch (kv->kv_type) {
			case KEY_TYPE_PATH:
				if (kv_setkey(match, "/") == -1)
					goto fail;
				break;
			case KEY_TYPE_COOKIE:
			case KEY_TYPE_HEADER:
				if (kv->kv_matchtree != NULL)
					match->kv_flags |= KV_FLAG_INVALID;
				else
					kv_free(match);
				match = kv->kv_match = NULL;
				break;
			default:
				/* query and url not supported */
				break;
			}
			break;
		case KEY_OPTION_HASH:
			switch (kv->kv_type) {
			case KEY_TYPE_PATH:
				value = match->kv_key;
				break;
			default:
				value = match->kv_value;
				break;
			}
			SipHash24_Update(&con->se_siphashctx,
			    value, strlen(value));
			break;
		case KEY_OPTION_LOG:
			/* perform this later */
			break;
		case KEY_OPTION_STRIP:
			nstrip = strtonum(kv->kv_value, 0, INT_MAX, NULL);
			if (kv->kv_type == KEY_TYPE_PATH) {
				if (kv_setkey(match, "%s",
				    server_root_strip(match->kv_key,
				    nstrip)) == -1)
					goto fail;
			}
			break;
		default:
			fatalx("%s: invalid action", __func__);
			/* NOTREACHED */
		}

		/* from now on, reads from kp writes to kv */
		if (kp == NULL)
			kp = kv;
		if (addkv && kv->kv_matchtree != NULL) {
			/* Add new entry to the list (eg. new HTTP header) */
			if ((match = kv_add(kv->kv_matchtree, kp->kv_key,
			    kp->kv_value, 0)) == NULL)
				goto fail;
			match->kv_option = kp->kv_option;
			match->kv_type = kp->kv_type;
			kv->kv_match = match;
		}
		if (match != NULL && kp->kv_flags & KV_FLAG_MACRO) {
			bzero(buf, sizeof(buf));
			if ((ptr = relay_expand_http(cre, kp->kv_value, buf,
			    sizeof(buf))) == NULL)
				goto fail;
			if (kv_set(match, "%s", ptr) == -1)
				goto fail;
		}

 matchdel:
		switch (kv->kv_option) {
		case KEY_OPTION_LOG:
			if (match == NULL)
				break;
			mp = &matchcopy;
			if (kv_inherit(mp, match) == NULL)
				goto fail;
			if (mp->kv_flags & KV_FLAG_INVALID) {
				if (kv_set(mp, "%s (removed)",
				    mp->kv_value) == -1)
					goto fail;
			}
			switch (kv->kv_type) {
			case KEY_TYPE_URL:
				key.kv_key = "Host";
				host = kv_find(&desc->http_headers, &key);
				switch (kv->kv_digest) {
				case DIGEST_NONE:
					if (host == NULL ||
					    host->kv_value == NULL)
						break;
					if (kv_setkey(mp, "%s%s",
					    host->kv_value, mp->kv_key) ==
					    -1)
						goto fail;
					break;
				default:
					if (kv_setkey(mp, "%s", kv->kv_key)
					    == -1)
						goto fail;
					break;
				}
				break;
			default:
				break;
			}
			if (kv_log(con, mp, con->se_label, cre->dir)
			    == -1)
				goto fail;
			break;
		default:
			break;
		}

		/* actions applied, cleanup kv */
		kv->kv_match = NULL;
		kv->kv_matchtree = NULL;
		TAILQ_REMOVE(actions, kv, kv_match_entry);

		kv_free(&kvcopy);
		kv_free(&matchcopy);
	}

	/*
	 * Change the backend if the forward table has been changed.
	 * This only works in the request direction.
	 */
	if (cre->dir == RELAY_DIR_REQUEST && con->se_table != tbl) {
		relay_reset_event(con, &con->se_out);
		con->se_table = tbl;
		con->se_haslog = 1;
	}

	/*
	 * log tag for request and response, request method
	 * and end of request marker ","
	 */
	if ((con->se_log != NULL) &&
	    ((meth = relay_httpmethod_byid(desc->http_method)) != NULL) &&
	    (asprintf(&msg, " %s", meth) != -1))
		evbuffer_add(con->se_log, msg, strlen(msg));
	free(msg);
	relay_log(con, cre->dir == RELAY_DIR_REQUEST ? "" : ";");
	ret = 0;
 fail:
	kv_free(&kvcopy);
	kv_free(&matchcopy);

	return (ret);
}

#define	RELAY_GET_SKIP_STEP(i)						\
	do {								\
		r = r->rule_skip[i];					\
		DPRINTF("%s:%d: skip %d rules", __func__, __LINE__, i);	\
	} while (0)

#define	RELAY_GET_NEXT_STEP						\
	do {								\
		DPRINTF("%s:%d: next rule", __func__, __LINE__);	\
		goto nextrule;						\
	} while (0)

int
relay_test(struct protocol *proto, struct ctl_relay_event *cre)
{
	struct rsession		*con;
	struct http_descriptor	*desc = cre->desc;
	struct relay_rule	*r = NULL, *rule = NULL;
	struct relay_table	*tbl = NULL;
	u_int			 action = RES_PASS;
	struct kvlist		 actions, matches;
	struct kv		*kv;
	int			 res = 0;

	con = cre->con;
	TAILQ_INIT(&actions);

	r = TAILQ_FIRST(&proto->rules);
	while (r != NULL) {
		TAILQ_INIT(&matches);
		TAILQ_INIT(&r->rule_kvlist);

		if (r->rule_dir && r->rule_dir != cre->dir)
			RELAY_GET_SKIP_STEP(RULE_SKIP_DIR);
		else if (proto->type != r->rule_proto)
			RELAY_GET_SKIP_STEP(RULE_SKIP_PROTO);
		else if (RELAY_AF_NEQ(r->rule_af, cre->ss.ss_family) ||
		     RELAY_AF_NEQ(r->rule_af, cre->dst->ss.ss_family))
			RELAY_GET_SKIP_STEP(RULE_SKIP_AF);
		else if (RELAY_ADDR_CMP(&r->rule_src, &cre->ss) != 0)
			RELAY_GET_SKIP_STEP(RULE_SKIP_SRC);
		else if (RELAY_ADDR_CMP(&r->rule_dst, &con->se_sockname) != 0)
			RELAY_GET_SKIP_STEP(RULE_SKIP_DST);
		else if (r->rule_method != HTTP_METHOD_NONE &&
		    (desc->http_method == HTTP_METHOD_RESPONSE ||
		     desc->http_method != r->rule_method))
			RELAY_GET_SKIP_STEP(RULE_SKIP_METHOD);
		else if (r->rule_tagged && con->se_tag != r->rule_tagged)
			RELAY_GET_NEXT_STEP;
		else if (relay_httpheader_test(cre, r, &matches) != 0)
			RELAY_GET_NEXT_STEP;
		else if ((res = relay_httpquery_test(cre, r, &matches)) != 0)
			RELAY_GET_NEXT_STEP;
		else if (relay_httppath_test(cre, r, &matches) != 0)
			RELAY_GET_NEXT_STEP;
		else if ((res = relay_httpurl_test(cre, r, &matches)) != 0)
			RELAY_GET_NEXT_STEP;
		else if ((res = relay_httpcookie_test(cre, r, &matches)) != 0)
			RELAY_GET_NEXT_STEP;
		else {
			DPRINTF("%s: session %d: matched rule %d",
			    __func__, con->se_id, r->rule_id);

			if (r->rule_action == RULE_ACTION_MATCH) {
				if (relay_match_actions(cre, r, &matches,
				    &actions, &tbl) != 0) {
					/* Something bad happened, drop */
					action = RES_DROP;
					break;
				}
				RELAY_GET_NEXT_STEP;
			} else if (r->rule_action == RULE_ACTION_BLOCK)
				action = RES_DROP;
			else if (r->rule_action == RULE_ACTION_PASS)
				action = RES_PASS;

			/* Rule matched */
			rule = r;

			/* Temporarily save actions */
			TAILQ_FOREACH(kv, &matches, kv_match_entry) {
				TAILQ_INSERT_TAIL(&rule->rule_kvlist,
				    kv, kv_rule_entry);
			}

			if (rule->rule_flags & RULE_FLAG_QUICK)
				break;

 nextrule:
			/* Continue to find last matching policy */
			DPRINTF("%s: session %d, res %d", __func__,
			    con->se_id, res);
			if (res == RES_BAD || res == RES_INTERNAL)
				return (res);
			res = 0;
			r = TAILQ_NEXT(r, rule_entry);
		}
	}

	if (rule != NULL && relay_match_actions(cre, rule, NULL, &actions, &tbl)
	    != 0) {
		/* Something bad happened, drop */
		action = RES_DROP;
	}

	if (relay_apply_actions(cre, &actions, tbl) != 0) {
		/* Something bad happened, drop */
		action = RES_DROP;
	}

	DPRINTF("%s: session %d: action %d", __func__,
	    con->se_id, action);

	return (action);
}

#define	RELAY_SET_SKIP_STEPS(i)						\
	do {								\
		while (head[i] != cur) {				\
			head[i]->rule_skip[i] = cur;			\
			head[i] = TAILQ_NEXT(head[i], rule_entry);	\
		}							\
	} while (0)

/* This code is derived from pf_calc_skip_steps() from pf.c */
void
relay_calc_skip_steps(struct relay_rules *rules)
{
	struct relay_rule	*head[RULE_SKIP_COUNT], *cur, *prev;
	int			 i;

	cur = TAILQ_FIRST(rules);
	prev = cur;
	for (i = 0; i < RULE_SKIP_COUNT; ++i)
		head[i] = cur;
	while (cur != NULL) {
		if (cur->rule_dir != prev->rule_dir)
			RELAY_SET_SKIP_STEPS(RULE_SKIP_DIR);
		else if (cur->rule_proto != prev->rule_proto)
			RELAY_SET_SKIP_STEPS(RULE_SKIP_PROTO);
		else if (RELAY_AF_NEQ(cur->rule_af, prev->rule_af))
			RELAY_SET_SKIP_STEPS(RULE_SKIP_AF);
		else if (RELAY_ADDR_NEQ(&cur->rule_src, &prev->rule_src))
			RELAY_SET_SKIP_STEPS(RULE_SKIP_SRC);
		else if (RELAY_ADDR_NEQ(&cur->rule_dst, &prev->rule_dst))
			RELAY_SET_SKIP_STEPS(RULE_SKIP_DST);
		else if (cur->rule_method != prev->rule_method)
			RELAY_SET_SKIP_STEPS(RULE_SKIP_METHOD);

		prev = cur;
		cur = TAILQ_NEXT(cur, rule_entry);
	}
	for (i = 0; i < RULE_SKIP_COUNT; ++i)
		RELAY_SET_SKIP_STEPS(i);
}

void
relay_match(struct kvlist *actions, struct kv *kv, struct kv *match,
    struct kvtree *matchtree)
{
	if (kv->kv_option != KEY_OPTION_NONE) {
		kv->kv_match = match;
		kv->kv_matchtree = matchtree;
		TAILQ_INSERT_TAIL(actions, kv, kv_match_entry);
	}
}

char *
server_root_strip(char *path, int n)
{
	char *p;

	/* Strip strip leading directories. Leading '/' is ignored. */
	for (; n > 0 && *path != '\0'; n--)
		if ((p = strchr(++path, '/')) != NULL)
			path = p;
		else
			path--;

	return (path);
}

