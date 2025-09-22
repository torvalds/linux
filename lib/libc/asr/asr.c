/*	$OpenBSD: asr.c,v 1.69 2025/06/18 13:50:02 deraadt Exp $	*/
/*
 * Copyright (c) 2010-2012 Eric Faurot <eric@openbsd.org>
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
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <netdb.h>

#include <asr.h>
#include <errno.h>
#include <fcntl.h>
#include <resolv.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "asr_private.h"

#include "thread_private.h"

#define DEFAULT_CONF		"lookup file\n"
#define DEFAULT_LOOKUP		"lookup bind file"

#define RELOAD_DELAY		15 /* seconds */

static void asr_check_reload(struct asr *);
static struct asr_ctx *asr_ctx_create(void);
static void asr_ctx_ref(struct asr_ctx *);
static void asr_ctx_free(struct asr_ctx *);
static int asr_ctx_add_searchdomain(struct asr_ctx *, const char *);
static int asr_ctx_from_fd(struct asr_ctx *, int);
static int asr_ctx_from_string(struct asr_ctx *, const char *);
static int asr_ctx_parse(struct asr_ctx *, const char *);
static int asr_parse_nameserver(struct sockaddr *, const char *);
static int asr_ndots(const char *);
static void pass0(char **, int, struct asr_ctx *);
static int strsplit(char *, char **, int);
static void asr_ctx_envopts(struct asr_ctx *);
static void *__THREAD_NAME(_asr);

static struct asr *_asr = NULL;

/* Allocate and configure an async "resolver". */
static void *
_asr_resolver(void)
{
	static int	 init = 0;
	struct asr	*asr;

	if (init == 0) {
#ifdef DEBUG
		if (getenv("ASR_DEBUG"))
			_asr_debug = stderr;
#endif
		init = 1;
	}

	if ((asr = calloc(1, sizeof(*asr))) == NULL)
		goto fail;

	asr_check_reload(asr);
	if (asr->a_ctx == NULL) {
		if ((asr->a_ctx = asr_ctx_create()) == NULL)
			goto fail;
		if (asr_ctx_from_string(asr->a_ctx, DEFAULT_CONF) == -1)
			goto fail;
		asr_ctx_envopts(asr->a_ctx);
	}

#ifdef DEBUG
	_asr_dump_config(_asr_debug, asr);
#endif
	return (asr);

    fail:
	if (asr) {
		if (asr->a_ctx)
			asr_ctx_free(asr->a_ctx);
		free(asr);
	}

	return (NULL);
}

/*
 * Free the "asr" async resolver (or the thread-local resolver if NULL).
 * Drop the reference to the current context.
 */
void
_asr_resolver_done(void *arg)
{
	struct asr_ctx *ac = arg;
	struct asr *asr;
	struct asr **priv;

	if (ac) {
		_asr_ctx_unref(ac);
		return;
	} else {
		priv = _THREAD_PRIVATE_DT(_asr, _asr, NULL, &_asr);
		if (*priv == NULL)
			return;
		asr = *priv;
		*priv = NULL;
	}

	_asr_ctx_unref(asr->a_ctx);
	free(asr);
}

static void
_asr_resolver_done_tp(void *arg)
{
	struct asr **priv = arg;
	struct asr *asr;

	if (*priv == NULL)
		return;
	asr = *priv;

	_asr_ctx_unref(asr->a_ctx);
	free(asr);
	free(priv);
}

void *
asr_resolver_from_string(const char *str)
{
	struct asr_ctx *ac;

	if ((ac = asr_ctx_create()) == NULL)
		return NULL;

	if (asr_ctx_from_string(ac, str) == -1) {
		asr_ctx_free(ac);
		return NULL;
	}

	return ac;
}
DEF_WEAK(asr_resolver_from_string);

void
asr_resolver_free(void *arg)
{
	_asr_ctx_unref(arg);
}
DEF_WEAK(asr_resolver_free);

/*
 * Cancel an async query.
 */
void
asr_abort(struct asr_query *as)
{
	_asr_async_free(as);
}

/*
 * Resume the "as" async query resolution.  Return one of ASYNC_COND,
 * or ASYNC_DONE and put query-specific return values in the user-allocated
 * memory at "ar".
 */
int
asr_run(struct asr_query *as, struct asr_result *ar)
{
	int	r, saved_errno = errno;

	memset(ar, 0, sizeof(*ar));

	DPRINT("asr: asr_run(%p, %p) %s ctx=[%p]\n", as, ar,
	    _asr_querystr(as->as_type), as->as_ctx);
	r = as->as_run(as, ar);

	DPRINT("asr: asr_run(%p, %p) -> %s", as, ar, _asr_transitionstr(r));
#ifdef DEBUG
	if (r == ASYNC_COND)
#endif
		DPRINT(" fd=%i timeout=%i", ar->ar_fd, ar->ar_timeout);
	DPRINT("\n");
	if (r == ASYNC_DONE)
		_asr_async_free(as);

	errno = saved_errno;

	return (r);
}
DEF_WEAK(asr_run);

static int
poll_intrsafe(struct pollfd *fds, nfds_t nfds, int timeout)
{
	struct timespec pollstart, pollend, elapsed;
	int r;

	if (WRAP(clock_gettime)(CLOCK_MONOTONIC, &pollstart))
		return -1;

	while ((r = poll(fds, 1, timeout)) == -1 && errno == EINTR) {
		if (WRAP(clock_gettime)(CLOCK_MONOTONIC, &pollend))
			return -1;
		timespecsub(&pollend, &pollstart, &elapsed);
		timeout -= elapsed.tv_sec * 1000 + elapsed.tv_nsec / 1000000;
		if (timeout < 1)
			return 0;
	}

	return r;
}

/*
 * Same as asr_run, but run in a loop that handles the fd conditions result.
 */
int
asr_run_sync(struct asr_query *as, struct asr_result *ar)
{
	struct pollfd	 fds[1];
	int		 r, saved_errno = errno;

	while ((r = asr_run(as, ar)) == ASYNC_COND) {
		fds[0].fd = ar->ar_fd;
		fds[0].events = (ar->ar_cond == ASR_WANT_READ) ? POLLIN:POLLOUT;

		if (poll_intrsafe(fds, 1, ar->ar_timeout) == -1) {
			memset(ar, 0, sizeof(*ar));
			ar->ar_errno = errno;
			ar->ar_h_errno = NETDB_INTERNAL;
			ar->ar_gai_errno = EAI_SYSTEM;
			ar->ar_rrset_errno = NETDB_INTERNAL;
			_asr_async_free(as);
			errno = saved_errno;
			return ASYNC_DONE;
		}

		/*
		 * Otherwise, just ignore the error and let asr_run()
		 * catch the failure.
		 */
	}

	errno = saved_errno;

	return (r);
}
DEF_WEAK(asr_run_sync);

/*
 * Create a new async request of the given "type" on the async context "ac".
 * Take a reference on it so it does not get deleted while the async query
 * is running.
 */
struct asr_query *
_asr_async_new(struct asr_ctx *ac, int type)
{
	struct asr_query	*as;

	DPRINT("asr: asr_async_new(ctx=%p) type=%i refcount=%i\n", ac, type,
	    ac ? ac->ac_refcount : 0);
	if (ac == NULL || (as = calloc(1, sizeof(*as))) == NULL)
		return (NULL);

	ac->ac_refcount += 1;
	as->as_ctx = ac;
	as->as_fd = -1;
	as->as_type = type;
	as->as_state = ASR_STATE_INIT;

	return (as);
}

/*
 * Free an async query and unref the associated context.
 */
void
_asr_async_free(struct asr_query *as)
{
	DPRINT("asr: asr_async_free(%p)\n", as);

	if (as->as_subq)
		_asr_async_free(as->as_subq);

	switch (as->as_type) {
	case ASR_SEND:
		if (as->as_fd != -1)
			close(as->as_fd);
		if (as->as.dns.obuf && !(as->as_flags & ASYNC_EXTOBUF))
			free(as->as.dns.obuf);
		if (as->as.dns.ibuf)
			free(as->as.dns.ibuf);
		if (as->as.dns.dname)
			free(as->as.dns.dname);
		break;

	case ASR_SEARCH:
		if (as->as.search.name)
			free(as->as.search.name);
		break;

	case ASR_GETRRSETBYNAME:
		if (as->as.rrset.name)
			free(as->as.rrset.name);
		break;

	case ASR_GETHOSTBYNAME:
	case ASR_GETHOSTBYADDR:
		if (as->as.hostnamadr.name)
			free(as->as.hostnamadr.name);
		break;

	case ASR_GETADDRINFO:
		if (as->as.ai.aifirst)
			freeaddrinfo(as->as.ai.aifirst);
		if (as->as.ai.hostname)
			free(as->as.ai.hostname);
		if (as->as.ai.servname)
			free(as->as.ai.servname);
		if (as->as.ai.fqdn)
			free(as->as.ai.fqdn);
		break;

	case ASR_GETNAMEINFO:
		break;
	}

	_asr_ctx_unref(as->as_ctx);
	free(as);
}

/*
 * Get a context from the given resolver. This takes a new reference to
 * the returned context, which *must* be explicitly dropped when done
 * using this context.
 */
struct asr_ctx *
_asr_use_resolver(void *arg)
{
	struct asr_ctx *ac = arg;
	struct asr *asr;
	struct asr **priv;

	if (ac) {
		asr_ctx_ref(ac);
		return ac;
	}
	else {
		DPRINT("using thread-local resolver\n");
		priv = _THREAD_PRIVATE_DT(_asr, _asr, _asr_resolver_done_tp,
		    &_asr);
		if (*priv == NULL) {
			DPRINT("setting up thread-local resolver\n");
			*priv = _asr_resolver();
		}
		asr = *priv;
	}
	if (asr != NULL) {
		asr_check_reload(asr);
		asr_ctx_ref(asr->a_ctx);
		return (asr->a_ctx);
	}
	return (NULL);
}

static void
asr_ctx_ref(struct asr_ctx *ac)
{
	DPRINT("asr: asr_ctx_ref(ctx=%p) refcount=%i\n", ac, ac->ac_refcount);
	ac->ac_refcount += 1;
}

/*
 * Drop a reference to an async context, freeing it if the reference
 * count drops to 0.
 */
void
_asr_ctx_unref(struct asr_ctx *ac)
{
	DPRINT("asr: asr_ctx_unref(ctx=%p) refcount=%i\n", ac,
	    ac ? ac->ac_refcount : 0);
	if (ac == NULL)
		return;
	if (--ac->ac_refcount)
		return;

	asr_ctx_free(ac);
}

static void
asr_ctx_free(struct asr_ctx *ac)
{
	int i;

	if (ac->ac_domain)
		free(ac->ac_domain);
	for (i = 0; i < ASR_MAXNS; i++)
		free(ac->ac_ns[i]);
	for (i = 0; i < ASR_MAXDOM; i++)
		free(ac->ac_dom[i]);

	free(ac);
}

/*
 * Reload the configuration file if it has changed on disk.
 */
static void
asr_check_reload(struct asr *asr)
{
	struct asr_ctx	*ac;
	struct stat	 st;
	struct timespec	 ts;
	pid_t		 pid;
	int		 fd;

	pid = getpid();
	if (pid != asr->a_pid) {
		asr->a_pid = pid;
		asr->a_rtime = 0;
	}

	if (WRAP(clock_gettime)(CLOCK_MONOTONIC, &ts) == -1)
		return;

	if ((ts.tv_sec - asr->a_rtime) < RELOAD_DELAY && asr->a_rtime != 0)
		return;
	asr->a_rtime = ts.tv_sec;

	DPRINT("asr: checking for update of \"%s\"\n", _PATH_RESCONF);
	fd = open(_PATH_RESCONF, O_RDONLY|O_CLOEXEC);
	if (fd == -1)
		return;
	if (fstat(fd, &st) == -1 ||
	    asr->a_mtime == st.st_mtime ||
	    (ac = asr_ctx_create()) == NULL) {
		close(fd);
		return;
	}
	asr->a_mtime = st.st_mtime;

	DPRINT("asr: reloading config file\n");
	if (asr_ctx_from_fd(ac, fd) == -1) {
		asr_ctx_free(ac);
		return;
	}

	asr_ctx_envopts(ac);
	if (asr->a_ctx)
		_asr_ctx_unref(asr->a_ctx);
	asr->a_ctx = ac;
}

/*
 * Construct a fully-qualified domain name for the given name and domain.
 * If "name" ends with a '.' it is considered as a FQDN by itself.
 * Otherwise, the domain, which must be a FQDN, is appended to "name" (it
 * may have a leading dot which would be ignored). If the domain is null,
 * then "." is used. Return the length of the constructed FQDN or (0) on
 * error.
 */
size_t
_asr_make_fqdn(const char *name, const char *domain, char *buf, size_t buflen)
{
	size_t	len;

	if (domain == NULL)
		domain = ".";
	else if ((len = strlen(domain)) == 0)
		return (0);
	else if (domain[len -1] != '.')
		return (0);

	len = strlen(name);
	if (len == 0) {
		if (strlcpy(buf, domain, buflen) >= buflen)
			return (0);
	} else if (name[len - 1] !=  '.') {
		if (domain[0] == '.')
			domain += 1;
		if (strlcpy(buf, name, buflen) >= buflen ||
		    strlcat(buf, ".", buflen) >= buflen ||
		    strlcat(buf, domain, buflen) >= buflen)
			return (0);
	} else {
		if (strlcpy(buf, name, buflen) >= buflen)
			return (0);
	}

	return (strlen(buf));
}

/*
 * Count the dots in a string.
 */
static int
asr_ndots(const char *s)
{
	int n;

	for (n = 0; *s; s++)
		if (*s == '.')
			n += 1;

	return (n);
}

/*
 * Allocate a new empty context.
 */
static struct asr_ctx *
asr_ctx_create(void)
{
	struct asr_ctx	*ac;

	if ((ac = calloc(1, sizeof(*ac))) == NULL)
		return (NULL);

	ac->ac_options = RES_RECURSE | RES_DEFNAMES | RES_DNSRCH;
	ac->ac_refcount = 1;
	ac->ac_ndots = 1;
	ac->ac_family[0] = AF_INET;
	ac->ac_family[1] = AF_INET6;
	ac->ac_family[2] = -1;

	ac->ac_nscount = 0;
	ac->ac_nstimeout = 5;
	ac->ac_nsretries = 4;

	return (ac);
}

struct asr_ctx *
_asr_no_resolver(void)
{
	return asr_ctx_create();
}

/*
 * Add a search domain to the async context.
 */
static int
asr_ctx_add_searchdomain(struct asr_ctx *ac, const char *domain)
{
	char buf[MAXDNAME];

	if (ac->ac_domcount == ASR_MAXDOM)
		return (-1);

	if (_asr_make_fqdn(domain, NULL, buf, sizeof(buf)) == 0)
		return (-1);

	if ((ac->ac_dom[ac->ac_domcount] = strdup(buf)) == NULL)
		return (0);

	ac->ac_domcount += 1;

	return (1);
}

static int
strsplit(char *line, char **tokens, int ntokens)
{
	int	ntok;
	char	*cp, **tp;

	for (cp = line, tp = tokens, ntok = 0;
	    ntok < ntokens && (*tp = strsep(&cp, " \t")) != NULL; )
		if (**tp != '\0') {
			tp++;
			ntok++;
		}

	return (ntok);
}

/*
 * Pass on a split config line.
 */
static void
pass0(char **tok, int n, struct asr_ctx *ac)
{
	int		 i, j, d;
	const char	*e;
	struct sockaddr_storage	ss;

	if (!strcmp(tok[0], "nameserver")) {
		if (ac->ac_nscount == ASR_MAXNS)
			return;
		if (n != 2)
			return;
		if (asr_parse_nameserver((struct sockaddr *)&ss, tok[1]))
			return;
		if ((ac->ac_ns[ac->ac_nscount] = calloc(1, ss.ss_len)) == NULL)
			return;
		memmove(ac->ac_ns[ac->ac_nscount], &ss, ss.ss_len);
		ac->ac_nscount += 1;

	} else if (!strcmp(tok[0], "domain")) {
		if (n != 2)
			return;
		if (ac->ac_domain)
			return;
		ac->ac_domain = strdup(tok[1]);

	} else if (!strcmp(tok[0], "lookup")) {
		/* ensure that each lookup is only given once */
		for (i = 1; i < n; i++)
			for (j = i + 1; j < n; j++)
				if (!strcmp(tok[i], tok[j]))
					return;
		ac->ac_dbcount = 0;
		for (i = 1; i < n && ac->ac_dbcount < ASR_MAXDB; i++) {
			if (!strcmp(tok[i], "yp")) {
				/* silently deprecated */
			} else if (!strcmp(tok[i], "bind"))
				ac->ac_db[ac->ac_dbcount++] = ASR_DB_DNS;
			else if (!strcmp(tok[i], "file"))
				ac->ac_db[ac->ac_dbcount++] = ASR_DB_FILE;
		}
	} else if (!strcmp(tok[0], "search")) {
		/* resolv.conf says the last line wins */
		for (i = 0; i < ASR_MAXDOM; i++) {
			free(ac->ac_dom[i]);
			ac->ac_dom[i] = NULL;
		}
		ac->ac_domcount = 0;
		for (i = 1; i < n; i++)
			asr_ctx_add_searchdomain(ac, tok[i]);

	} else if (!strcmp(tok[0], "family")) {
		if (n == 1 || n > 3)
			return;
		for (i = 1; i < n; i++)
			if (strcmp(tok[i], "inet4") && strcmp(tok[i], "inet6"))
				return;
		for (i = 1; i < n; i++)
			ac->ac_family[i - 1] = strcmp(tok[i], "inet4") ? \
			    AF_INET6 : AF_INET;
		ac->ac_family[i - 1] = -1;

	} else if (!strcmp(tok[0], "options")) {
		for (i = 1; i < n; i++) {
			if (!strcmp(tok[i], "tcp"))
				ac->ac_options |= RES_USEVC;
			else if (!strcmp(tok[i], "edns0"))
				ac->ac_options |= RES_USE_EDNS0;
			else if ((!strncmp(tok[i], "ndots:", 6))) {
				e = NULL;
				d = strtonum(tok[i] + 6, 1, 16, &e);
				if (e == NULL)
					ac->ac_ndots = d;
			} else if (!strcmp(tok[i], "trust-ad"))
				ac->ac_options |= RES_TRUSTAD;
		}
	}
}

/*
 * Setup an async context with the config specified in the string "str".
 */
static int
asr_ctx_from_string(struct asr_ctx *ac, const char *str)
{
	struct sockaddr_in6	*sin6;
	struct sockaddr_in	*sin;
	int			 i, trustad;
	char			 buf[512], *ch;

	asr_ctx_parse(ac, str);

	if (ac->ac_dbcount == 0) {
		/* No lookup directive */
		asr_ctx_parse(ac, DEFAULT_LOOKUP);
	}

	if (ac->ac_nscount == 0)
		asr_ctx_parse(ac, "nameserver 127.0.0.1");

	if (ac->ac_domain == NULL)
		if (gethostname(buf, sizeof buf) == 0) {
			ch = strchr(buf, '.');
			if (ch)
				ac->ac_domain = strdup(ch + 1);
			else /* Assume root. see resolv.conf(5) */
				ac->ac_domain = strdup("");
		}

	/* If no search domain was specified, use the local subdomains */
	if (ac->ac_domcount == 0)
		for (ch = ac->ac_domain; ch; ) {
			asr_ctx_add_searchdomain(ac, ch);
			ch = strchr(ch, '.');
			if (ch && asr_ndots(++ch) == 0)
				break;
		}

	trustad = 1;
	for (i = 0; i < ac->ac_nscount && trustad; i++) {
		switch (ac->ac_ns[i]->sa_family) {
		case AF_INET:
			sin = (struct sockaddr_in *)ac->ac_ns[i];
			if (sin->sin_addr.s_addr != htonl(INADDR_LOOPBACK))
				trustad = 0;
			break;
		case AF_INET6:
			sin6 = (struct sockaddr_in6 *)ac->ac_ns[i];
			if (!IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr))
				trustad = 0;
			break;
		default:
			trustad = 0;
			break;
		}
	}
	if (trustad)
		ac->ac_options |= RES_TRUSTAD;

	return (0);
}

/*
 * Setup the "ac" async context from the file at location "path".
 * Takes control of fd, so must close() if it encounters error
 */
static int
asr_ctx_from_fd(struct asr_ctx *ac, int fd)
{
	FILE	*cf;
	char	 buf[4096];
	ssize_t	 r;

	cf = fdopen(fd, "r");
	if (cf == NULL) {
		close(fd);
		return (-1);
	}

	r = fread(buf, 1, sizeof buf - 1, cf);
	if (feof(cf) == 0) {
		DPRINT("asr: config file too long: \"%s\"\n", path);
		r = -1;
	}
	fclose(cf);
	if (r == -1)
		return (-1);
	buf[r] = '\0';

	return asr_ctx_from_string(ac, buf);
}

/*
 * Parse lines in the configuration string. For each one, split it into
 * tokens and pass them to "pass0" for processing.
 */
static int
asr_ctx_parse(struct asr_ctx *ac, const char *str)
{
	size_t		 len;
	const char	*line;
	char		 buf[1024];
	char		*tok[10];
	int		 ntok;

	line = str;
	while (*line) {
		len = strcspn(line, "\n\0");
		if (len < sizeof buf) {
			memmove(buf, line, len);
			buf[len] = '\0';
		} else
			buf[0] = '\0';
		line += len;
		if (*line == '\n')
			line++;
		buf[strcspn(buf, ";#")] = '\0';
		if ((ntok = strsplit(buf, tok, 10)) == 0)
			continue;

		pass0(tok, ntok, ac);
	}

	return (0);
}

/*
 * Check for environment variables altering the configuration as described
 * in resolv.conf(5).  Although not documented there, this feature is disabled
 * for setuid/setgid programs.
 */
static void
asr_ctx_envopts(struct asr_ctx *ac)
{
	char	buf[4096], *e;
	size_t	s;

	if (issetugid()) {
		ac->ac_options |= RES_NOALIASES;
		return;
	}

	if ((e = getenv("RES_OPTIONS")) != NULL) {
		strlcpy(buf, "options ", sizeof buf);
		strlcat(buf, e, sizeof buf);
		s = strlcat(buf, "\n", sizeof buf);
		if (s < sizeof buf)
			asr_ctx_parse(ac, buf);
	}

	if ((e = getenv("LOCALDOMAIN")) != NULL) {
		strlcpy(buf, "search ", sizeof buf);
		strlcat(buf, e, sizeof buf);
		s = strlcat(buf, "\n", sizeof buf);
		if (s < sizeof buf)
			asr_ctx_parse(ac, buf);
	}
}

/*
 * Parse a resolv.conf(5) nameserver string into a sockaddr.
 */
static int
asr_parse_nameserver(struct sockaddr *sa, const char *s)
{
	in_port_t	 portno = 53;

	if (_asr_sockaddr_from_str(sa, PF_UNSPEC, s) == -1)
		return (-1);

	if (sa->sa_family == PF_INET)
		((struct sockaddr_in *)sa)->sin_port = htons(portno);
	else if (sa->sa_family == PF_INET6)
		((struct sockaddr_in6 *)sa)->sin6_port = htons(portno);

	return (0);
}

/*
 * Turn a (uncompressed) DNS domain name into a regular nul-terminated string
 * where labels are separated by dots. The result is put into the "buf" buffer,
 * truncated if it exceeds "max" chars. The function returns "buf".
 */
char *
_asr_strdname(const char *_dname, char *buf, size_t max)
{
	const unsigned char *dname = _dname;
	char	*res;
	size_t	 left, count;

	if (_dname[0] == 0) {
		strlcpy(buf, ".", max);
		return buf;
	}

	res = buf;
	left = max - 1;
	while (dname[0] && left) {
		count = (dname[0] < (left - 1)) ? dname[0] : (left - 1);
		memmove(buf, dname + 1, count);
		dname += dname[0] + 1;
		left -= count;
		buf += count;
		if (left) {
			left -= 1;
			*buf++ = '.';
		}
	}
	buf[0] = 0;

	return (res);
}

/*
 * Read and split the next line from the given namedb file.
 * Return -1 on error, or put the result in the "tokens" array of
 * size "ntoken" and returns the number of token on the line.
 */
int
_asr_parse_namedb_line(FILE *file, char **tokens, int ntoken, char *lbuf, size_t sz)
{
	size_t	  len;
	char	 *buf;
	int	  ntok;

    again:
	if ((buf = fgetln(file, &len)) == NULL)
		return (-1);

	if (len >= sz)
		goto again;

	if (buf[len - 1] == '\n')
		len--;
	else {
		memcpy(lbuf, buf, len);
		buf = lbuf;
	}

	buf[len] = '\0';
	buf[strcspn(buf, "#")] = '\0';
	if ((ntok = strsplit(buf, tokens, ntoken)) == 0)
		goto again;

	return (ntok);
}

/*
 * Update the async context so that it uses the next configured DB.
 * Return 0 on success, or -1 if no more DBs is available.
 */
int
_asr_iter_db(struct asr_query *as)
{
	if (as->as_db_idx >= as->as_ctx->ac_dbcount) {
		DPRINT("asr_iter_db: done\n");
		return (-1);
	}

	as->as_db_idx += 1;
	DPRINT("asr_iter_db: %i\n", as->as_db_idx);

	return (0);
}
