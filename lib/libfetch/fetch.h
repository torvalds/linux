/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1998-2004 Dag-Erling Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _FETCH_H_INCLUDED
#define _FETCH_H_INCLUDED

#define _LIBFETCH_VER "libfetch/2.0"

#define URL_SCHEMELEN 16
#define URL_USERLEN 256
#define URL_PWDLEN 256

struct url {
	char		 scheme[URL_SCHEMELEN+1];
	char		 user[URL_USERLEN+1];
	char		 pwd[URL_PWDLEN+1];
	char		 host[MAXHOSTNAMELEN+1];
	int		 port;
	char		*doc;
	off_t		 offset;
	size_t		 length;
	time_t		 ims_time;
	int		 netrcfd;
};

struct url_stat {
	off_t		 size;
	time_t		 atime;
	time_t		 mtime;
};

struct url_ent {
	char		 name[PATH_MAX];
	struct url_stat	 stat;
};

/* Recognized schemes */
#define SCHEME_FTP	"ftp"
#define SCHEME_HTTP	"http"
#define SCHEME_HTTPS	"https"
#define SCHEME_FILE	"file"

/* Error codes */
#define	FETCH_ABORT	 1
#define	FETCH_AUTH	 2
#define	FETCH_DOWN	 3
#define	FETCH_EXISTS	 4
#define	FETCH_FULL	 5
#define	FETCH_INFO	 6
#define	FETCH_MEMORY	 7
#define	FETCH_MOVED	 8
#define	FETCH_NETWORK	 9
#define	FETCH_OK	10
#define	FETCH_PROTO	11
#define	FETCH_RESOLV	12
#define	FETCH_SERVER	13
#define	FETCH_TEMP	14
#define	FETCH_TIMEOUT	15
#define	FETCH_UNAVAIL	16
#define	FETCH_UNKNOWN	17
#define	FETCH_URL	18
#define	FETCH_VERBOSE	19

__BEGIN_DECLS

/* FILE-specific functions */
FILE		*fetchXGetFile(struct url *, struct url_stat *, const char *);
FILE		*fetchGetFile(struct url *, const char *);
FILE		*fetchPutFile(struct url *, const char *);
int		 fetchStatFile(struct url *, struct url_stat *, const char *);
struct url_ent	*fetchListFile(struct url *, const char *);

/* HTTP-specific functions */
FILE		*fetchXGetHTTP(struct url *, struct url_stat *, const char *);
FILE		*fetchGetHTTP(struct url *, const char *);
FILE		*fetchPutHTTP(struct url *, const char *);
int		 fetchStatHTTP(struct url *, struct url_stat *, const char *);
struct url_ent	*fetchListHTTP(struct url *, const char *);
FILE		*fetchReqHTTP(struct url *, const char *, const char *,
		    const char *, const char *);

/* FTP-specific functions */
FILE		*fetchXGetFTP(struct url *, struct url_stat *, const char *);
FILE		*fetchGetFTP(struct url *, const char *);
FILE		*fetchPutFTP(struct url *, const char *);
int		 fetchStatFTP(struct url *, struct url_stat *, const char *);
struct url_ent	*fetchListFTP(struct url *, const char *);

/* Generic functions */
FILE		*fetchXGetURL(const char *, struct url_stat *, const char *);
FILE		*fetchGetURL(const char *, const char *);
FILE		*fetchPutURL(const char *, const char *);
int		 fetchStatURL(const char *, struct url_stat *, const char *);
struct url_ent	*fetchListURL(const char *, const char *);
FILE		*fetchXGet(struct url *, struct url_stat *, const char *);
FILE		*fetchGet(struct url *, const char *);
FILE		*fetchPut(struct url *, const char *);
int		 fetchStat(struct url *, struct url_stat *, const char *);
struct url_ent	*fetchList(struct url *, const char *);

/* URL parsing */
struct url	*fetchMakeURL(const char *, const char *, int,
		     const char *, const char *, const char *);
struct url	*fetchParseURL(const char *);
void		 fetchFreeURL(struct url *);

__END_DECLS

/* Authentication */
typedef int (*auth_t)(struct url *);
extern auth_t		 fetchAuthMethod;

/* Last error code */
extern int		 fetchLastErrCode;
#define MAXERRSTRING 256
extern char		 fetchLastErrString[MAXERRSTRING];

/* I/O timeout */
extern int		 fetchTimeout;

/* Restart interrupted syscalls */
extern int		 fetchRestartCalls;

/* Extra verbosity */
extern int		 fetchDebug;

#endif
