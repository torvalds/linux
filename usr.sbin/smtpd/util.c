/*	$OpenBSD: util.c,v 1.159 2024/06/02 23:26:39 jsg Exp $	*/

/*
 * Copyright (c) 2000,2001 Markus Friedl.  All rights reserved.
 * Copyright (c) 2008 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2009 Jacek Masiulaniec <jacekm@dobremiasto.net>
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
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

#include <sys/stat.h>

#include <netinet/in.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fts.h>
#include <libgen.h>
#include <resolv.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "smtpd.h"
#include "log.h"

static int  parse_mailname_file(char *, size_t);

int	tracing = 0;
int	foreground_log = 0;

void *
xmalloc(size_t size)
{
	void	*r;

	if ((r = malloc(size)) == NULL)
		fatal("malloc");

	return (r);
}

void *
xcalloc(size_t nmemb, size_t size)
{
	void	*r;

	if ((r = calloc(nmemb, size)) == NULL)
		fatal("calloc");

	return (r);
}

char *
xstrdup(const char *str)
{
	char	*r;

	if ((r = strdup(str)) == NULL)
		fatal("strdup");

	return (r);
}

void *
xmemdup(const void *ptr, size_t size)
{
	void	*r;

	if ((r = malloc(size)) == NULL)
		fatal("malloc");

	memmove(r, ptr, size);

	return (r);
}

int
xasprintf(char **ret, const char *format, ...)
{
	int r;
	va_list ap;

	va_start(ap, format);
	r = vasprintf(ret, format, ap);
	va_end(ap);
	if (r == -1)
		fatal("vasprintf");

	return (r);
}


#if !defined(NO_IO)
int
io_xprintf(struct io *io, const char *fmt, ...)
{
	va_list	ap;
	int len;

	va_start(ap, fmt);
	len = io_vprintf(io, fmt, ap);
	va_end(ap);
	if (len == -1)
		fatal("io_xprintf(%p, %s, ...)", io, fmt);

	return len;
}

int
io_xprint(struct io *io, const char *str)
{
	int len;

	len = io_print(io, str);
	if (len == -1)
		fatal("io_xprint(%p, %s, ...)", io, str);

	return len;
}
#endif

char *
strip(char *s)
{
	size_t	 l;

	while (isspace((unsigned char)*s))
		s++;

	for (l = strlen(s); l; l--) {
		if (!isspace((unsigned char)s[l-1]))
			break;
		s[l-1] = '\0';
	}

	return (s);
}

int
bsnprintf(char *str, size_t size, const char *format, ...)
{
	int ret;
	va_list ap;

	va_start(ap, format);
	ret = vsnprintf(str, size, format, ap);
	va_end(ap);
	if (ret < 0 || (size_t)ret >= size)
		return 0;

	return 1;
}


int
ckdir(const char *path, mode_t mode, uid_t owner, gid_t group, int create)
{
	char		mode_str[12];
	int		ret;
	struct stat	sb;

	if (stat(path, &sb) == -1) {
		if (errno != ENOENT || create == 0) {
			log_warn("stat: %s", path);
			return (0);
		}

		/* chmod is deferred to avoid umask effect */
		if (mkdir(path, 0) == -1) {
			log_warn("mkdir: %s", path);
			return (0);
		}

		if (chown(path, owner, group) == -1) {
			log_warn("chown: %s", path);
			return (0);
		}

		if (chmod(path, mode) == -1) {
			log_warn("chmod: %s", path);
			return (0);
		}

		if (stat(path, &sb) == -1) {
			log_warn("stat: %s", path);
			return (0);
		}
	}

	ret = 1;

	/* check if it's a directory */
	if (!S_ISDIR(sb.st_mode)) {
		ret = 0;
		log_warnx("%s is not a directory", path);
	}

	/* check that it is owned by owner/group */
	if (sb.st_uid != owner) {
		ret = 0;
		log_warnx("%s is not owned by uid %d", path, owner);
	}
	if (sb.st_gid != group) {
		ret = 0;
		log_warnx("%s is not owned by gid %d", path, group);
	}

	/* check permission */
	if ((sb.st_mode & 07777) != mode) {
		ret = 0;
		strmode(mode, mode_str);
		mode_str[10] = '\0';
		log_warnx("%s must be %s (%o)", path, mode_str + 1, mode);
	}

	return ret;
}

int
rmtree(char *path, int keepdir)
{
	char		*path_argv[2];
	FTS		*fts;
	FTSENT		*e;
	int		 ret, depth;

	path_argv[0] = path;
	path_argv[1] = NULL;
	ret = 0;
	depth = 0;

	fts = fts_open(path_argv, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
	if (fts == NULL) {
		log_warn("fts_open: %s", path);
		return (-1);
	}

	while ((e = fts_read(fts)) != NULL) {
		switch (e->fts_info) {
		case FTS_D:
			depth++;
			break;
		case FTS_DP:
		case FTS_DNR:
			depth--;
			if (keepdir && depth == 0)
				continue;
			if (rmdir(e->fts_path) == -1) {
				log_warn("rmdir: %s", e->fts_path);
				ret = -1;
			}
			break;

		case FTS_F:
			if (unlink(e->fts_path) == -1) {
				log_warn("unlink: %s", e->fts_path);
				ret = -1;
			}
		}
	}

	fts_close(fts);

	return (ret);
}

int
mvpurge(char *from, char *to)
{
	size_t		 n;
	int		 retry;
	const char	*sep;
	char		 buf[PATH_MAX];

	if ((n = strlen(to)) == 0)
		fatalx("to is empty");

	sep = (to[n - 1] == '/') ? "" : "/";
	retry = 0;

again:
	(void)snprintf(buf, sizeof buf, "%s%s%u", to, sep, arc4random());
	if (rename(from, buf) == -1) {
		/* ENOTDIR has actually 2 meanings, and incorrect input
		 * could lead to an infinite loop. Consider that after
		 * 20 tries something is hopelessly wrong.
		 */
		if (errno == ENOTEMPTY || errno == EISDIR || errno == ENOTDIR) {
			if ((retry++) >= 20)
				return (-1);
			goto again;
		}
		return -1;
	}

	return 0;
}


int
mktmpfile(void)
{
	char		path[PATH_MAX];
	int		fd;

	if (!bsnprintf(path, sizeof(path), "%s/smtpd.XXXXXXXXXX",
		PATH_TEMPORARY)) {
		log_warn("snprintf");
		fatal("exiting");
	}

	if ((fd = mkstemp(path)) == -1) {
		log_warn("cannot create temporary file %s", path);
		fatal("exiting");
	}
	unlink(path);
	return (fd);
}


/* Close file, signifying temporary error condition (if any) to the caller. */
int
safe_fclose(FILE *fp)
{
	if (ferror(fp)) {
		fclose(fp);
		return 0;
	}
	if (fflush(fp)) {
		fclose(fp);
		if (errno == ENOSPC)
			return 0;
		fatal("safe_fclose: fflush");
	}
	if (fsync(fileno(fp)))
		fatal("safe_fclose: fsync");
	if (fclose(fp))
		fatal("safe_fclose: fclose");

	return 1;
}

int
hostname_match(const char *hostname, const char *pattern)
{
	while (*pattern != '\0' && *hostname != '\0') {
		if (*pattern == '*') {
			while (*pattern == '*')
				pattern++;
			while (*hostname != '\0' &&
			    tolower((unsigned char)*hostname) !=
			    tolower((unsigned char)*pattern))
				hostname++;
			continue;
		}

		if (tolower((unsigned char)*pattern) !=
		    tolower((unsigned char)*hostname))
			return 0;
		pattern++;
		hostname++;
	}

	return (*hostname == '\0' && *pattern == '\0');
}

int
mailaddr_match(const struct mailaddr *maddr1, const struct mailaddr *maddr2)
{
	struct mailaddr m1 = *maddr1;
	struct mailaddr m2 = *maddr2;
	char	       *p;

	/* catchall */
	if (m2.user[0] == '\0' && m2.domain[0] == '\0')
		return 1;

	if (m2.domain[0] && !hostname_match(m1.domain, m2.domain))
		return 0;

	if (m2.user[0]) {
		/* if address from table has a tag, we must respect it */
		if (strchr(m2.user, *env->sc_subaddressing_delim) == NULL) {
			/* otherwise, strip tag from session address if any */
			p = strchr(m1.user, *env->sc_subaddressing_delim);
			if (p)
				*p = '\0';
		}
		if (strcasecmp(m1.user, m2.user))
			return 0;
	}
	return 1;
}

int
valid_localpart(const char *s)
{
#define IS_ATEXT(c) (isalnum((unsigned char)(c)) || strchr(MAILADDR_ALLOWED, (c)))
nextatom:
	if (!IS_ATEXT(*s) || *s == '\0')
		return 0;
	while (*(++s) != '\0') {
		if (*s == '.')
			break;
		if (IS_ATEXT(*s))
			continue;
		return 0;
	}
	if (*s == '.') {
		s++;
		goto nextatom;
	}
	return 1;
}

int
valid_domainpart(const char *s)
{
	struct in_addr	 ina;
	struct in6_addr	 ina6;
	char		*c, domain[SMTPD_MAXDOMAINPARTSIZE];
	const char	*p;
	size_t		 dlen;

	if (*s == '[') {
		if (strncasecmp("[IPv6:", s, 6) == 0)
			p = s + 6;
		else
			p = s + 1;

		if (strlcpy(domain, p, sizeof domain) >= sizeof domain)
			return 0;

		c = strchr(domain, ']');
		if (!c || c[1] != '\0')
			return 0;

		*c = '\0';

		if (inet_pton(AF_INET6, domain, &ina6) == 1)
			return 1;
		if (inet_pton(AF_INET, domain, &ina) == 1)
			return 1;

		return 0;
	}

	if (*s == '\0')
		return 0;

	dlen = strlen(s);
	if (dlen >= sizeof domain)
		return 0;

	if (s[dlen - 1] == '.')
		return 0;

	return res_hnok(s);
}

#define LABELCHR(c) ((c) == '-' || (c) == '_' || isalpha((unsigned char)(c)) || isdigit((unsigned char)(c)))
#define LABELMAX 63
#define DNAMEMAX 253

int
valid_domainname(const char *str)
{
	const char *label, *s;

	/*
	 * Expect a sequence of dot-separated labels, possibly with a trailing
	 * dot. The empty string is rejected, as well a single dot.
	 */
	for (s = str; *s; s++) {

		/* Start of a new label. */
		label = s;
		while (LABELCHR(*s))
			s++;

		/* Must have at least one char and at most LABELMAX. */
		if (s == label || s - label > LABELMAX)
			return 0;

		/* If last label, stop here. */
		if (*s == '\0')
			break;

		/* Expect a dot as label separator or last char. */
		if (*s != '.')
			return 0;
	}

	/* Must have at leat one label and no more than DNAMEMAX chars. */
	if (s == str || s - str > DNAMEMAX)
		return 0;

	return 1;
}

int
valid_smtp_response(const char *s)
{
	if (strlen(s) < 5)
		return 0;

	if ((s[0] < '2' || s[0] > '5') ||
	    (s[1] < '0' || s[1] > '9') ||
	    (s[2] < '0' || s[2] > '9') ||
	    (s[3] != ' '))
		return 0;

	return 1;
}

int
valid_xtext(const char *s)
{
	for (; *s != '\0'; ++s) {
		if (*s < '!' || *s > '~' || *s == '=')
			return 0;

		if (*s != '+')
			continue;

		s++;
		if (!isdigit((unsigned char)*s) &&
		    !(*s >= 'A' && *s <= 'F'))
			return 0;

		s++;
		if (!isdigit((unsigned char)*s) &&
		    !(*s >= 'A' && *s <= 'F'))
			return 0;
	}

	return 1;
}

int
secure_file(int fd, char *path, char *userdir, uid_t uid, int mayread)
{
	char		 buf[PATH_MAX];
	char		 homedir[PATH_MAX];
	struct stat	 st;
	char		*cp;

	if (realpath(path, buf) == NULL)
		return 0;

	if (realpath(userdir, homedir) == NULL)
		homedir[0] = '\0';

	/* Check the open file to avoid races. */
	if (fstat(fd, &st) == -1 ||
	    !S_ISREG(st.st_mode) ||
	    st.st_uid != uid ||
	    (st.st_mode & (mayread ? 022 : 066)) != 0)
		return 0;

	/* For each component of the canonical path, walking upwards. */
	for (;;) {
		if ((cp = dirname(buf)) == NULL)
			return 0;
		(void)strlcpy(buf, cp, sizeof(buf));

		if (stat(buf, &st) == -1 ||
		    (st.st_uid != 0 && st.st_uid != uid) ||
		    (st.st_mode & 022) != 0)
			return 0;

		/* We can stop checking after reaching homedir level. */
		if (strcmp(homedir, buf) == 0)
			break;

		/*
		 * dirname should always complete with a "/" path,
		 * but we can be paranoid and check for "." too
		 */
		if ((strcmp("/", buf) == 0) || (strcmp(".", buf) == 0))
			break;
	}

	return 1;
}

void
addargs(arglist *args, char *fmt, ...)
{
	va_list ap;
	char *cp;
	uint nalloc;
	int r;
	char	**tmp;

	va_start(ap, fmt);
	r = vasprintf(&cp, fmt, ap);
	va_end(ap);
	if (r == -1)
		fatal("addargs: argument too long");

	nalloc = args->nalloc;
	if (args->list == NULL) {
		nalloc = 32;
		args->num = 0;
	} else if (args->num+2 >= nalloc)
		nalloc *= 2;

	tmp = reallocarray(args->list, nalloc, sizeof(char *));
	if (tmp == NULL)
		fatal("addargs: reallocarray");
	args->list = tmp;
	args->nalloc = nalloc;
	args->list[args->num++] = cp;
	args->list[args->num] = NULL;
}

int
lowercase(char *buf, const char *s, size_t len)
{
	if (len == 0)
		return 0;

	if (strlcpy(buf, s, len) >= len)
		return 0;

	while (*buf != '\0') {
		*buf = tolower((unsigned char)*buf);
		buf++;
	}

	return 1;
}

int
uppercase(char *buf, const char *s, size_t len)
{
	if (len == 0)
		return 0;

	if (strlcpy(buf, s, len) >= len)
		return 0;

	while (*buf != '\0') {
		*buf = toupper((unsigned char)*buf);
		buf++;
	}

	return 1;
}

void
xlowercase(char *buf, const char *s, size_t len)
{
	if (len == 0)
		fatalx("lowercase: len == 0");

	if (!lowercase(buf, s, len))
		fatalx("lowercase: truncation");
}

uint64_t
generate_uid(void)
{
	static uint32_t id;
	static uint8_t	inited;
	uint64_t	uid;

	if (!inited) {
		id = arc4random();
		inited = 1;
	}
	while ((uid = ((uint64_t)(id++) << 32 | arc4random())) == 0)
		;

	return (uid);
}

int
session_socket_error(int fd)
{
	int		error;
	socklen_t	len;

	len = sizeof(error);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) == -1)
		fatal("session_socket_error: getsockopt");

	return (error);
}

const char *
parse_smtp_response(char *line, size_t len, char **msg, int *cont)
{
	if (len >= LINE_MAX)
		return "line too long";

	if (len > 3) {
		if (msg)
			*msg = line + 4;
		if (cont)
			*cont = (line[3] == '-');
	} else if (len == 3) {
		if (msg)
			*msg = line + 3;
		if (cont)
			*cont = 0;
	} else
		return "line too short";

	/* validate reply code */
	if (line[0] < '2' || line[0] > '5' || !isdigit((unsigned char)line[1]) ||
	    !isdigit((unsigned char)line[2]))
		return "reply code out of range";

	return NULL;
}

static int
parse_mailname_file(char *hostname, size_t len)
{
	FILE	*fp;
	char	*buf = NULL;
	size_t	 bufsz = 0;
	ssize_t	 buflen;

	if ((fp = fopen(MAILNAME_FILE, "r")) == NULL)
		return 1;

	buflen = getline(&buf, &bufsz, fp);
	fclose(fp);
	if (buflen == -1) {
		free(buf);
		return 1;
	}

	if (buf[buflen - 1] == '\n')
		buf[buflen - 1] = '\0';

	bufsz = strlcpy(hostname, buf, len);
	free(buf);
	if (bufsz >= len) {
		fprintf(stderr, MAILNAME_FILE " entry too long");
		return 1;
	}

	return 0;
}

int
getmailname(char *hostname, size_t len)
{
	struct addrinfo	 hints, *res = NULL;
	int		 error;

	/* Try MAILNAME_FILE first */
	if (parse_mailname_file(hostname, len) == 0)
		return 0;

	/* Next, gethostname(3) */
	if (gethostname(hostname, len) == -1) {
		fprintf(stderr, "getmailname: gethostname() failed\n");
		return -1;
	}

	if (strchr(hostname, '.') != NULL)
		return 0;

	/* Canonicalize if domain part is missing */
	memset(&hints, 0, sizeof hints);
	hints.ai_family = PF_UNSPEC;
	hints.ai_flags = AI_CANONNAME;
	error = getaddrinfo(hostname, NULL, &hints, &res);
	if (error)
		return 0; /* Continue with non-canon hostname */

	if (strlcpy(hostname, res->ai_canonname, len) >= len) {
		fprintf(stderr, "hostname too long");
		freeaddrinfo(res);
		return -1;
	}

	freeaddrinfo(res);
	return 0;
}

int
base64_encode(unsigned char const *src, size_t srclen,
	      char *dest, size_t destsize)
{
	return __b64_ntop(src, srclen, dest, destsize);
}

int
base64_decode(char const *src, unsigned char *dest, size_t destsize)
{
	return __b64_pton(src, dest, destsize);
}

int
base64_encode_rfc3548(unsigned char const *src, size_t srclen,
	      char *dest, size_t destsize)
{
	size_t i;
	int ret;

	if ((ret = base64_encode(src, srclen, dest, destsize)) == -1)
		return -1;

	for (i = 0; i < destsize; ++i) {
		if (dest[i] == '/')
			dest[i] = '_';
		else if (dest[i] == '+')
			dest[i] = '-';
	}

	return ret;
}

void
log_trace0(const char *emsg, ...)
{
	va_list	 ap;

	va_start(ap, emsg);
	vlog(LOG_DEBUG, emsg, ap);
	va_end(ap);
}

void
log_trace_verbose(int v)
{
	tracing = v;

	/* Set debug logging in log.c */
	log_setverbose(v & TRACE_DEBUG ? 2 : foreground_log);
}

int
parse_table_line(FILE *fp, char **line, size_t *linesize,
    int *type, char **key, char **val, int *malformed)
{
	char	*keyp, *valp;
	ssize_t	 linelen;

	*key = NULL;
	*val = NULL;
	*malformed = 0;

	if ((linelen = getline(line, linesize, fp)) == -1)
		return (-1);

	keyp = *line;
	while (isspace((unsigned char)*keyp)) {
		++keyp;
		--linelen;
	}
	if (*keyp == '\0')
		return 0;
	while (linelen > 0 && isspace((unsigned char)keyp[linelen - 1]))
		keyp[--linelen] = '\0';
	if (*keyp == '#') {
		if (*type == T_NONE) {
			keyp++;
			while (isspace((unsigned char)*keyp))
				++keyp;
			if (!strcmp(keyp, "@list"))
				*type = T_LIST;
		}
		return 0;
	}

	if (*keyp == '[') {
		if ((valp = strchr(keyp, ']')) == NULL) {
			*malformed = 1;
			return (0);
		}
		valp++;
	} else
		valp = keyp + strcspn(keyp, " \t:");

	if (*type == T_NONE)
		*type = (*valp == '\0') ? T_LIST : T_HASH;

	if (*type == T_LIST) {
		*key = keyp;
		return (0);
	}

	/* T_HASH */
	if (*valp != '\0') {
		*valp++ = '\0';
		valp += strspn(valp, " \t");
	}
	if (*valp == '\0')
		*malformed = 1;

	*key = keyp;
	*val = valp;
	return (0);
}
