/*	$OpenBSD: server_file.c,v 1.80 2024/04/29 16:17:46 florian Exp $	*/

/*
 * Copyright (c) 2006 - 2017 Reyk Floeter <reyk@openbsd.org>
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
#include <sys/stat.h>

#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <dirent.h>
#include <time.h>
#include <event.h>
#include <util.h>

#include "httpd.h"
#include "http.h"
#include "css.h"
#include "js.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))
#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

int		 server_file_access(struct httpd *, struct client *,
		    char *, size_t);
int		 server_file_request(struct httpd *, struct client *,
		    struct media_type *, int, const struct stat *);
int		 server_partial_file_request(struct httpd *, struct client *,
		    struct media_type *, int, const struct stat *,
		    char *);
int		 server_file_index(struct httpd *, struct client *, int,
		    struct stat *);
int		 server_file_modified_since(struct http_descriptor *,
		    const struct timespec *);
int		 server_file_method(struct client *);
int		 parse_range_spec(char *, size_t, struct range *);
int		 parse_ranges(struct client *, char *, size_t);
static int	 select_visible(const struct dirent *);

int
server_file_access(struct httpd *env, struct client *clt,
    char *path, size_t len)
{
	struct http_descriptor	*desc = clt->clt_descreq;
	struct server_config	*srv_conf = clt->clt_srv_conf;
	struct stat		 st;
	struct kv		*r, key;
	struct media_type	*media;
	char			*newpath, *encodedpath;
	int			 ret, fd;

	if ((fd = open(path, O_RDONLY)) == -1) {
		switch (errno) {
		case ENOENT:
		case ENOTDIR:
			return (404);
		case EACCES:
			return (403);
		default:
			return (500);
		}
	}
	if (fstat(fd, &st) == -1) {
		close(fd);
		return (500);
	}

	if (S_ISDIR(st.st_mode)) {
		/* Deny access if directory indexing is disabled */
		if (srv_conf->flags & SRVFLAG_NO_INDEX) {
			close(fd);
			return (403);
		}

		if (desc->http_path_alias != NULL) {
			/* Recursion - the index "file" is a directory? */
			close(fd);
			return (500);
		}

		/* Redirect to path with trailing "/" */
		if (path[strlen(path) - 1] != '/') {
			close(fd);
			if ((encodedpath = url_encode(desc->http_path)) == NULL)
				return (500);
			if (asprintf(&newpath, "%s/", encodedpath) == -1) {
				free(encodedpath);
				return (500);
			}
			free(encodedpath);

			/* Path alias will be used for the redirection */
			desc->http_path_alias = newpath;

			/* Indicate that the file has been moved */
			return (301);
		}

		/* Append the default index file to the location */
		if (asprintf(&newpath, "%s%s", desc->http_path,
		    srv_conf->index) == -1) {
			close(fd);
			return (500);
		}
		desc->http_path_alias = newpath;
		if (server_getlocation(clt, newpath) != srv_conf) {
			/* The location has changed */
			close(fd);
			return (server_file(env, clt));
		}

		/* Otherwise append the default index file to the path */
		if (strlcat(path, srv_conf->index, len) >= len) {
			close(fd);
			return (403);
		}

		ret = server_file_access(env, clt, path, len);
		if (ret == 404) {
			/*
			 * Index file not found; fail if auto-indexing is
			 * not enabled, otherwise return success but
			 * indicate directory with S_ISDIR of the previous
			 * stat.
			 */
			if ((srv_conf->flags & SRVFLAG_AUTO_INDEX) == 0) {
				close(fd);
				return (403);
			}

			return (server_file_index(env, clt, fd, &st));
		}
		close(fd);
		return (ret);
	} else if (!S_ISREG(st.st_mode)) {
		/* Don't follow symlinks and ignore special files */
		close(fd);
		return (403);
	}

	media = media_find_config(env, srv_conf, path);

	/* Only consider range requests for GET */
	if (desc->http_method == HTTP_METHOD_GET) {
		key.kv_key = "Range";
		r = kv_find(&desc->http_headers, &key);
		if (r != NULL)
			return (server_partial_file_request(env, clt, media,
			    fd, &st, r->kv_value));
	}

	/* change path to path.gz if necessary. */
	if (srv_conf->flags & SRVFLAG_GZIP_STATIC) {
		struct http_descriptor	*req = clt->clt_descreq;
		struct http_descriptor	*resp = clt->clt_descresp;
		struct stat		 gzst;
		int			 gzfd;
		char			 gzpath[PATH_MAX];

		/* check Accept-Encoding header */
		key.kv_key = "Accept-Encoding";
		r = kv_find(&req->http_headers, &key);

		if (r != NULL && strstr(r->kv_value, "gzip") != NULL) {
			/* append ".gz" to path and check existence */
			ret = snprintf(gzpath, sizeof(gzpath), "%s.gz", path);
			if (ret < 0 || (size_t)ret >= sizeof(gzpath)) {
				close(fd);
				return (500);
			}

			if ((gzfd = open(gzpath, O_RDONLY)) != -1) {
				/* .gz must be a file, and not older */
				if (fstat(gzfd, &gzst) != -1 &&
				    S_ISREG(gzst.st_mode) &&
				    timespeccmp(&gzst.st_mtim, &st.st_mtim,
				    >=)) {
					kv_add(&resp->http_headers,
					    "Content-Encoding", "gzip");
					/* Use original file timestamp */
					gzst.st_mtim = st.st_mtim;
					st = gzst;
					close(fd);
					fd = gzfd;
				} else {
					close(gzfd);
				}
			}
		}
	}

	return (server_file_request(env, clt, media, fd, &st));
}

int
server_file(struct httpd *env, struct client *clt)
{
	struct http_descriptor	*desc = clt->clt_descreq;
	struct server_config	*srv_conf = clt->clt_srv_conf;
	char			 path[PATH_MAX];
	const char		*stripped, *errstr = NULL;
	int			 ret = 500;

	if (srv_conf->flags & SRVFLAG_FCGI)
		return (server_fcgi(env, clt));

	/* Request path is already canonicalized */
	stripped = server_root_strip(
	    desc->http_path_alias != NULL ?
	    desc->http_path_alias : desc->http_path,
	    srv_conf->strip);
	if ((size_t)snprintf(path, sizeof(path), "%s%s",
	    srv_conf->root, stripped) >= sizeof(path)) {
		errstr = desc->http_path;
		goto abort;
	}

	/* Returns HTTP status code on error */
	if ((ret = server_file_access(env, clt, path, sizeof(path))) > 0) {
		errstr = desc->http_path_alias != NULL ?
		    desc->http_path_alias : desc->http_path;
		goto abort;
	}

	return (ret);

 abort:
	if (errstr == NULL)
		errstr = strerror(errno);
	server_abort_http(clt, ret, errstr);
	return (-1);
}

int
server_file_method(struct client *clt)
{
	struct http_descriptor	*desc = clt->clt_descreq;

	switch (desc->http_method) {
	case HTTP_METHOD_GET:
	case HTTP_METHOD_HEAD:
		return (0);
	default:
		/* Other methods are not allowed */
		errno = EACCES;
		return (405);
	}
	/* NOTREACHED */
}

int
server_file_request(struct httpd *env, struct client *clt, struct media_type
    *media, int fd, const struct stat *st)
{
	struct server_config	*srv_conf = clt->clt_srv_conf;
	const char		*errstr = NULL;
	int			 ret, code = 500;
	size_t			 bufsiz;

	if ((ret = server_file_method(clt)) != 0) {
		code = ret;
		goto abort;
	}

	if ((ret = server_file_modified_since(clt->clt_descreq, &st->st_mtim))
	    != -1) {
		/* send the header without a body */
		if ((ret = server_response_http(clt, ret, media, -1,
		    MINIMUM(time(NULL), st->st_mtim.tv_sec))) == -1)
			goto fail;
		close(fd);
		goto done;
	}

	ret = server_response_http(clt, 200, media, st->st_size,
	    MINIMUM(time(NULL), st->st_mtim.tv_sec));
	switch (ret) {
	case -1:
		goto fail;
	case 0:
		/* Connection is already finished */
		close(fd);
		goto done;
	default:
		break;
	}

	clt->clt_fd = fd;
	if (clt->clt_srvbev != NULL)
		bufferevent_free(clt->clt_srvbev);

	clt->clt_srvbev_throttled = 0;
	clt->clt_srvbev = bufferevent_new(clt->clt_fd, server_read,
	    server_write, server_file_error, clt);
	if (clt->clt_srvbev == NULL) {
		errstr = "failed to allocate file buffer event";
		goto fail;
	}

	/* Adjust read watermark to the optimal file io size */
	bufsiz = MAXIMUM(st->st_blksize, 64 * 1024);
	bufferevent_setwatermark(clt->clt_srvbev, EV_READ, 0,
	    bufsiz);

	bufferevent_settimeout(clt->clt_srvbev,
	    srv_conf->timeout.tv_sec, srv_conf->timeout.tv_sec);
	bufferevent_enable(clt->clt_srvbev, EV_READ);
	bufferevent_disable(clt->clt_bev, EV_READ);

 done:
	server_reset_http(clt);
	return (0);
 fail:
	bufferevent_disable(clt->clt_bev, EV_READ|EV_WRITE);
	bufferevent_free(clt->clt_bev);
	clt->clt_bev = NULL;
 abort:
	if (fd != -1)
		close(fd);
	if (errstr == NULL)
		errstr = strerror(errno);
	server_abort_http(clt, code, errstr);
	return (-1);
}

int
server_partial_file_request(struct httpd *env, struct client *clt,
    struct media_type *media, int fd, const struct stat *st, char *range_str)
{
	struct server_config	*srv_conf = clt->clt_srv_conf;
	struct http_descriptor	*resp = clt->clt_descresp;
	struct media_type	 multipart_media;
	struct range_data	*r = &clt->clt_ranges;
	struct range		*range;
	size_t			 content_length = 0, bufsiz;
	int			 code = 500, i, nranges, ret;
	char			 content_range[64];
	const char		*errstr = NULL;

	if ((nranges = parse_ranges(clt, range_str, st->st_size)) < 1) {
		code = 416;
		(void)snprintf(content_range, sizeof(content_range),
		    "bytes */%lld", st->st_size);
		errstr = content_range;
		goto abort;
	}

	r->range_media = media;

	if (nranges == 1) {
		range = &r->range[0];
		(void)snprintf(content_range, sizeof(content_range),
		    "bytes %lld-%lld/%lld", range->start, range->end,
		    st->st_size);
		if (kv_add(&resp->http_headers, "Content-Range",
		    content_range) == NULL)
			goto abort;

		range = &r->range[0];
		content_length += range->end - range->start + 1;
	} else {
		/* Add boundary, all parts will be handled by the callback */
		arc4random_buf(&clt->clt_boundary, sizeof(clt->clt_boundary));

		/* Calculate Content-Length of the complete multipart body */
		for (i = 0; i < nranges; i++) {
			range = &r->range[i];

			/* calculate Content-Length of the complete body */
			if ((ret = snprintf(NULL, 0,
			    "\r\n--%llu\r\n"
			    "Content-Type: %s/%s\r\n"
			    "Content-Range: bytes %lld-%lld/%lld\r\n\r\n",
			    clt->clt_boundary,
			    media->media_type, media->media_subtype,
			    range->start, range->end, st->st_size)) < 0)
				goto abort;

			/* Add data length */
			content_length += ret + range->end - range->start + 1;

		}
		if ((ret = snprintf(NULL, 0, "\r\n--%llu--\r\n",
		    clt->clt_boundary)) < 0)
			goto abort;
		content_length += ret;

		/* prepare multipart/byteranges media type */
		(void)strlcpy(multipart_media.media_type, "multipart",
		    sizeof(multipart_media.media_type));
		(void)snprintf(multipart_media.media_subtype,
		    sizeof(multipart_media.media_subtype),
		    "byteranges; boundary=%llu", clt->clt_boundary);
		media = &multipart_media;
	}

	/* Start with first range */
	r->range_toread = TOREAD_HTTP_RANGE;

	ret = server_response_http(clt, 206, media, content_length,
	    MINIMUM(time(NULL), st->st_mtim.tv_sec));
	switch (ret) {
	case -1:
		goto fail;
	case 0:
		/* Connection is already finished */
		close(fd);
		goto done;
	default:
		break;
	}

	clt->clt_fd = fd;
	if (clt->clt_srvbev != NULL)
		bufferevent_free(clt->clt_srvbev);

	clt->clt_srvbev_throttled = 0;
	clt->clt_srvbev = bufferevent_new(clt->clt_fd, server_read_httprange,
	    server_write, server_file_error, clt);
	if (clt->clt_srvbev == NULL) {
		errstr = "failed to allocate file buffer event";
		goto fail;
	}

	/* Adjust read watermark to the optimal file io size */
	bufsiz = MAXIMUM(st->st_blksize, 64 * 1024);
	bufferevent_setwatermark(clt->clt_srvbev, EV_READ, 0,
	    bufsiz);

	bufferevent_settimeout(clt->clt_srvbev,
	    srv_conf->timeout.tv_sec, srv_conf->timeout.tv_sec);
	bufferevent_enable(clt->clt_srvbev, EV_READ);
	bufferevent_disable(clt->clt_bev, EV_READ);

 done:
	server_reset_http(clt);
	return (0);
 fail:
	bufferevent_disable(clt->clt_bev, EV_READ|EV_WRITE);
	bufferevent_free(clt->clt_bev);
	clt->clt_bev = NULL;
 abort:
	if (fd != -1)
		close(fd);
	if (errstr == NULL)
		errstr = strerror(errno);
	server_abort_http(clt, code, errstr);
	return (-1);
}

/* ignore hidden files starting with a dot */
static int 
select_visible(const struct dirent *dp)
{
    if (dp->d_name[0] == '.' &&
	!(dp->d_name[1] == '.' && dp->d_name[2] == '\0'))
	    return 0;
    else
	    return 1;
}

int
server_file_index(struct httpd *env, struct client *clt, int fd,
    struct stat *st)
{
	char			  path[PATH_MAX];
	char			  tmstr[21];
	struct http_descriptor	 *desc = clt->clt_descreq;
	struct server_config	 *srv_conf = clt->clt_srv_conf;
	struct dirent		**namelist, *dp;
	int			  namesize, i, ret, skip;
	int			  code = 500;
	struct evbuffer		 *evb = NULL;
	struct media_type	 *media;
	const char		 *stripped;
	char			 *escapeduri, *escapedhtml, *escapedpath;
	struct tm		  tm;
	time_t			  t, dir_mtime;
	char 			  human_size[FMT_SCALED_STRSIZE];

	if ((ret = server_file_method(clt)) != 0) {
		code = ret;
		goto abort;
	}

	/* Request path is already canonicalized */
	stripped = server_root_strip(desc->http_path, srv_conf->strip);
	if ((size_t)snprintf(path, sizeof(path), "%s%s",
	    srv_conf->root, stripped) >= sizeof(path))
		goto abort;

	/* Save last modification time */
	dir_mtime = MINIMUM(time(NULL), st->st_mtim.tv_sec);

	if ((evb = evbuffer_new()) == NULL)
		goto abort;

	if ((escapedpath = escape_html(desc->http_path)) == NULL)
		goto abort;

	/* Generate simple HTML index document */
	if (evbuffer_add_printf(evb,
	    "<!DOCTYPE html>\n"
	    "<html lang=\"en\">\n"
	    "<head>\n"
	    "<meta charset=\"utf-8\">\n"
	    "<title>Index of %s</title>\n"
	    "<style><!--\n%s--></style>\n"
	    "</head>\n"
	    "<body>\n"
	    "<h1>Index of %s</h1>\n"
	    "<table><thead>\n"
	    "<tr class=\"sort\"><th class=\"sorted\">Name</th>\n"
	    "    <th>Date</th><th>Size</th></tr>\n"
	    "</thead><tbody>\n",
	    escapedpath, css, escapedpath) == -1) {
		free(escapedpath);
		goto abort;
	}

	free(escapedpath);

	if ((namesize = scandirat(fd, ".", &namelist, select_visible,
	    alphasort)) == -1)
		goto abort;

	/* Indicate failure but continue going through the list */
	skip = 0;

	for (i = 0; i < namesize; i++) {
		struct stat subst;

		dp = namelist[i];

		if (skip ||
		    fstatat(fd, dp->d_name, &subst, 0) == -1) {
			free(dp);
			continue;
		}

		t = subst.st_mtime;
		localtime_r(&t, &tm);
		strftime(tmstr, sizeof(tmstr), "%d-%h-%Y %R", &tm);

		if ((escapeduri = url_encode(dp->d_name)) == NULL) {
			skip = 1;
			free(dp);
			continue;
		}
		if ((escapedhtml = escape_html(dp->d_name)) == NULL) {
			skip = 1;
			free(escapeduri);
			free(dp);
			continue;
		}

		if (S_ISDIR(subst.st_mode)) {
			if (evbuffer_add_printf(evb,
			    "<tr class=\"dir\">"
			    "<td><a href=\"%s%s/\">%s/</a></td>\n"
			    "    <td data-o=\"%lld\">%s</td><td>%s</td></tr>\n",
			    strchr(escapeduri, ':') != NULL ? "./" : "",
			    escapeduri, escapedhtml, 
			    (long long)t, tmstr, "-") == -1)
				skip = 1;
		} else if (S_ISREG(subst.st_mode)) {
			if ((fmt_scaled(subst.st_size, human_size) != 0) ||
			   (evbuffer_add_printf(evb,
			    "<tr><td><a href=\"%s%s\">%s</a></td>\n"
			    "    <td data-o=\"%lld\">%s</td>"
			    "<td title=\"%llu\">%s</td></tr>\n",
			    strchr(escapeduri, ':') != NULL ? "./" : "",
			    escapeduri, escapedhtml,
			    (long long)t, tmstr, 
			    subst.st_size, human_size) == -1))
				skip = 1;
		}
		free(escapeduri);
		free(escapedhtml);
		free(dp);
	}
	free(namelist);

	if (skip ||
	    evbuffer_add_printf(evb,
	    "</tbody></table>\n<script>\n"
	    "%s\n"
	    "</script>\n</body>\n</html>\n", js) == -1)
		goto abort;

	close(fd);
	fd = -1;

	media = media_find_config(env, srv_conf, "index.html");
	ret = server_response_http(clt, 200, media, EVBUFFER_LENGTH(evb),
	    dir_mtime);
	switch (ret) {
	case -1:
		goto fail;
	case 0:
		/* Connection is already finished */
		evbuffer_free(evb);
		goto done;
	default:
		break;
	}

	if (server_bufferevent_write_buffer(clt, evb) == -1)
		goto fail;
	evbuffer_free(evb);
	evb = NULL;

	bufferevent_enable(clt->clt_bev, EV_READ|EV_WRITE);
	if (clt->clt_persist)
		clt->clt_toread = TOREAD_HTTP_HEADER;
	else
		clt->clt_toread = TOREAD_HTTP_NONE;
	clt->clt_done = 0;

 done:
	server_reset_http(clt);
	return (0);
 fail:
	bufferevent_disable(clt->clt_bev, EV_READ|EV_WRITE);
	bufferevent_free(clt->clt_bev);
	clt->clt_bev = NULL;
 abort:
	if (fd != -1)
		close(fd);
	if (evb != NULL)
		evbuffer_free(evb);
	server_abort_http(clt, code, desc->http_path);
	return (-1);
}

void
server_file_error(struct bufferevent *bev, short error, void *arg)
{
	struct client		*clt = arg;
	struct evbuffer		*src, *dst;

	if (error & EVBUFFER_TIMEOUT) {
		server_close(clt, "buffer event timeout");
		return;
	}
	if (error & EVBUFFER_ERROR) {
		if (errno == EFBIG) {
			bufferevent_enable(bev, EV_READ);
			return;
		}
		server_close(clt, "buffer event error");
		return;
	}
	if (error & (EVBUFFER_READ|EVBUFFER_WRITE|EVBUFFER_EOF)) {
		bufferevent_disable(bev, EV_READ|EV_WRITE);

		clt->clt_done = 1;

		src = EVBUFFER_INPUT(clt->clt_bev);

		/* Close the connection if a previous pipeline is empty */
		if (clt->clt_pipelining && EVBUFFER_LENGTH(src) == 0)
			clt->clt_persist = 0;

		if (clt->clt_persist) {
			/* Close input file and wait for next HTTP request */
			if (clt->clt_fd != -1)
				close(clt->clt_fd);
			clt->clt_fd = -1;
			clt->clt_toread = TOREAD_HTTP_HEADER;
			server_reset_http(clt);
			bufferevent_enable(clt->clt_bev, EV_READ|EV_WRITE);

			/* Start pipelining if the buffer is not empty */
			if (EVBUFFER_LENGTH(src)) {
				clt->clt_pipelining++;
				server_read_http(clt->clt_bev, arg);
			}
			return;
		}

		dst = EVBUFFER_OUTPUT(clt->clt_bev);
		if (EVBUFFER_LENGTH(dst)) {
			/* Finish writing all data first */
			bufferevent_enable(clt->clt_bev, EV_WRITE);
			return;
		}

		server_close(clt, "done");
		return;
	}
	server_close(clt, "unknown event error");
	return;
}

int
server_file_modified_since(struct http_descriptor *desc, const struct timespec
    *mtim)
{
	struct kv	 key, *since;
	struct tm	 tm;

	key.kv_key = "If-Modified-Since";
	if ((since = kv_find(&desc->http_headers, &key)) != NULL &&
	    since->kv_value != NULL) {
		memset(&tm, 0, sizeof(struct tm));

		/*
		 * Return "Not modified" if the file hasn't changed since
		 * the requested time.
		 */
		if (strptime(since->kv_value,
		    "%a, %d %h %Y %T %Z", &tm) != NULL &&
		    timegm(&tm) >= mtim->tv_sec)
			return (304);
	}

	return (-1);
}

int
parse_ranges(struct client *clt, char *str, size_t file_sz)
{
	int			 i = 0;
	char			*p, *q;
	struct range_data	*r = &clt->clt_ranges;

	memset(r, 0, sizeof(*r));

	/* Extract range unit */
	if ((p = strchr(str, '=')) == NULL)
		return (-1);

	*p++ = '\0';
	/* Check if it's a bytes range spec */
	if (strcmp(str, "bytes") != 0)
		return (-1);

	while ((q = strchr(p, ',')) != NULL) {
		*q++ = '\0';

		/* Extract start and end positions */
		if (parse_range_spec(p, file_sz, &r->range[i]) == 0)
			continue;

		i++;
		if (i == SERVER_MAX_RANGES)
			return (-1);

		p = q;
	}

	if (parse_range_spec(p, file_sz, &r->range[i]) != 0)
		i++;

	r->range_total = file_sz;
	r->range_count = i;
	return (i);
}

int
parse_range_spec(char *str, size_t size, struct range *r)
{
	size_t		 start_str_len, end_str_len;
	char		*p, *start_str, *end_str;
	const char	*errstr;

	if ((p = strchr(str, '-')) == NULL)
		return (0);

	*p++ = '\0';
	start_str = str;
	end_str = p;
	start_str_len = strlen(start_str);
	end_str_len = strlen(end_str);

	/* Either 'start' or 'end' is optional but not both */
	if ((start_str_len == 0) && (end_str_len == 0))
		return (0);

	if (end_str_len) {
		r->end = strtonum(end_str, 0, LLONG_MAX, &errstr);
		if (errstr)
			return (0);

		if ((size_t)r->end >= size)
			r->end = size - 1;
	} else
		r->end = size - 1;

	if (start_str_len) {
		r->start = strtonum(start_str, 0, LLONG_MAX, &errstr);
		if (errstr)
			return (0);

		if ((size_t)r->start >= size)
			return (0);
	} else {
		r->start = size - r->end;
		r->end = size - 1;
	}

	if (r->end < r->start)
		return (0);

	return (1);
}
