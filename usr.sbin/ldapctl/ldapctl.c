/*	$OpenBSD: ldapctl.c,v 1.21 2024/11/21 13:38:14 claudio Exp $	*/

/*
 * Copyright (c) 2009, 2010 Martin Hedenfalk <martin@bzero.se>
 * Copyright (c) 2007, 2008 Reyk Floeter <reyk@vantronix.net>
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2003 Henning Brauer <henning@openbsd.org>
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
#include <sys/queue.h>
#include <sys/un.h>
#include <sys/tree.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <event.h>

#include "ldapd.h"
#include "log.h"

enum action {
	NONE,
	SHOW_STATS,
	LOG_VERBOSE,
	LOG_BRIEF,
	COMPACT_DB,
	INDEX_DB
};

__dead void	 usage(void);
void		 show_stats(struct imsg *imsg);
void		 show_dbstats(const char *prefix, struct btree_stat *st);
void		 show_nsstats(struct imsg *imsg);
int		 compact_db(const char *path);
int		 compact_namespace(struct namespace *ns, const char *datadir);
int		 compact_namespaces(const char *datadir);
int		 index_namespace(struct namespace *ns, const char *datadir);
int		 index_namespaces(const char *datadir);
int		 ssl_load_certfile(struct ldapd_config *, const char *, u_int8_t);

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr,
	    "usage: %s [-v] [-f file] [-r directory] [-s socket] "
	    "command [argument ...]\n",
	    __progname);
	exit(1);
}

int
compact_db(const char *path)
{
	struct btree	*bt;
	int		 rc;

	log_info("compacting database %s", path);
	bt = btree_open(path, BT_NOSYNC | BT_REVERSEKEY, 0644);
	if (bt == NULL)
		return -1;

	do {
		if ((rc = btree_compact(bt)) == -1 && errno == EBUSY)
			usleep(100000);
	} while (rc == -1 && errno == EBUSY);

	btree_close(bt);
	return rc;
}

int
compact_namespace(struct namespace *ns, const char *datadir)
{
	char		*path;

	if (asprintf(&path, "%s/%s_data.db", datadir, ns->suffix) == -1)
		return -1;
	if (compact_db(path) != 0) {
		log_warn("%s", path);
		free(path);
		return -1;
	}
	free(path);

	if (asprintf(&path, "%s/%s_indx.db", datadir, ns->suffix) == -1)
		return -1;
	if (compact_db(path) != 0) {
		log_warn("%s", path);
		free(path);
		return -1;
	}
	free(path);

	return 0;
}

int
compact_namespaces(const char *datadir)
{
	struct namespace	*ns;

	TAILQ_FOREACH(ns, &conf->namespaces, next) {
		if (SLIST_EMPTY(&ns->referrals))
		    continue;
		if (compact_namespace(ns, datadir) != 0)
			return -1;
	}

	return 0;
}

int
index_namespace(struct namespace *ns, const char *datadir)
{
	struct btval		 key, val;
	struct btree		*data_db, *indx_db;
	struct cursor		*cursor;
	struct ber_element	*elm;
	char			*path;
	int			 i, rc;

	log_info("indexing namespace %s", ns->suffix);

	if (asprintf(&path, "%s/%s_data.db", datadir, ns->suffix) == -1)
		return -1;
	data_db = btree_open(path, BT_NOSYNC | BT_REVERSEKEY, 0644);
	free(path);
	if (data_db == NULL)
		return -1;

	if (asprintf(&path, "%s/%s_indx.db", datadir, ns->suffix) == -1)
		return -1;
	indx_db = btree_open(path, BT_NOSYNC, 0644);
	free(path);
	if (indx_db == NULL) {
		btree_close(data_db);
		return -1;
	}

	if ((cursor = btree_cursor_open(data_db)) == NULL) {
		btree_close(data_db);
		btree_close(indx_db);
		return -1;
	}

	bzero(&key, sizeof(key));
	bzero(&val, sizeof(val));

	for (;;) {
		for (;;) {
			ns->indx_txn = btree_txn_begin(indx_db, 0);
			if (ns->indx_txn == NULL && errno == EBUSY)
				usleep(100000);
			else
				break;
		}

		if (ns->indx_txn == NULL) {
			log_warn("failed to start transaction");
			break;
		}

		for (i = 0; i < 100; i++) {
			rc = btree_cursor_get(cursor, &key, &val, BT_NEXT);
			if (rc != BT_SUCCESS)
				break;
			if ((elm = db2ber(&val, ns->compression_level)) == NULL)
				continue;
			rc = index_entry(ns, &key, elm);
			ober_free_elements(elm);
			btval_reset(&key);
			btval_reset(&val);
			if (rc != 0)
				break;
		}

		if (btree_txn_commit(ns->indx_txn) != BT_SUCCESS)
			break;

		if (i != 100)
			break;
	}

	btree_cursor_close(cursor);
	btree_close(data_db);
	btree_close(indx_db);

	return 0;
}

int
index_namespaces(const char *datadir)
{
	struct namespace	*ns;

	TAILQ_FOREACH(ns, &conf->namespaces, next) {
		if (SLIST_EMPTY(&ns->referrals))
			continue;
		if (index_namespace(ns, datadir) != 0)
			return -1;
	}

	return 0;
}

int
ssl_load_certfile(struct ldapd_config *env, const char *name, u_int8_t flags)
{
	return 0;
}

int
main(int argc, char *argv[])
{
	int			 ctl_sock;
	int			 done = 0, verbose = 0, vlog = 0;
	ssize_t			 n;
	int			 ch;
	enum action		 action = NONE;
	const char		*datadir = DATADIR;
	struct stat		 sb;
	const char		*sock = LDAPD_SOCKET;
	char			*conffile = CONFFILE;
	struct sockaddr_un	 sun;
	struct imsg		 imsg;
	struct imsgbuf		 ibuf;

	log_init(1, 0);

	while ((ch = getopt(argc, argv, "f:r:s:v")) != -1) {
		switch (ch) {
		case 'f':
			conffile = optarg;
			break;
		case 'r':
			datadir = optarg;
			break;
		case 's':
			sock = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		usage();

	if (stat(datadir, &sb) == -1)
		err(1, "%s", datadir);
	if (!S_ISDIR(sb.st_mode))
		errx(1, "%s is not a directory", datadir);

	ldap_loginit(NULL, 1, verbose);

	if (strcmp(argv[0], "stats") == 0)
		action = SHOW_STATS;
	else if (strcmp(argv[0], "compact") == 0)
		action = COMPACT_DB;
	else if (strcmp(argv[0], "index") == 0)
		action = INDEX_DB;
	else if (strcmp(argv[0], "log") == 0) {
		if (argc != 2)
			usage();
		if (strcmp(argv[1], "verbose") == 0)
			action = LOG_VERBOSE;
		else if (strcmp(argv[1], "brief") == 0)
			action = LOG_BRIEF;
		else
			usage();
	} else
		usage();

	if (action == COMPACT_DB || action == INDEX_DB) {
		if (parse_config(conffile) != 0)
			exit(2);

		if (pledge("stdio rpath wpath cpath flock", NULL) == -1)
			err(1, "pledge");

		if (action == COMPACT_DB)
			return compact_namespaces(datadir);
		else
			return index_namespaces(datadir);
	}

	/* connect to ldapd control socket */
	if ((ctl_sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "socket");

	bzero(&sun, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, sock, sizeof(sun.sun_path));
	if (connect(ctl_sock, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(1, "connect: %s", sock);

	if (imsgbuf_init(&ibuf, ctl_sock) == -1)
		err(1, "imsgbuf_init");
	done = 0;

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	/* process user request */
	switch (action) {
	case SHOW_STATS:
		imsg_compose(&ibuf, IMSG_CTL_STATS, 0, 0, -1, NULL, 0);
		break;
	case LOG_VERBOSE:
		vlog = 1;
		/* FALLTHROUGH */
	case LOG_BRIEF:
		imsg_compose(&ibuf, IMSG_CTL_LOG_VERBOSE, 0, 0, -1,
		    &vlog, sizeof(vlog));
		printf("logging request sent.\n");
		done = 1;
		break;
	case NONE:
		break;
	case COMPACT_DB:
	case INDEX_DB:
		fatal("internal error");
	}

	if (imsgbuf_flush(&ibuf) == -1)
		err(1, "write error");

	while (!done) {
		if ((n = imsgbuf_read(&ibuf)) == -1)
			err(1, "read error");
		if (n == 0)
			errx(1, "pipe closed");

		while (!done) {
			if ((n = imsg_get(&ibuf, &imsg)) == -1)
				errx(1, "imsg_get error");
			if (n == 0)
				break;
			switch (imsg.hdr.type) {
			case IMSG_CTL_STATS:
				show_stats(&imsg);
				break;
			case IMSG_CTL_NSSTATS:
				show_nsstats(&imsg);
				break;
			case IMSG_CTL_END:
				done = 1;
				break;
			case NONE:
				break;
			}
			imsg_free(&imsg);
		}
	}
	close(ctl_sock);

	return (0);
}

void
show_stats(struct imsg *imsg)
{
	struct ldapd_stats	*st;

	st = imsg->data;

	printf("start time: %s", ctime(&st->started_at));
	printf("requests: %llu\n", st->requests);
	printf("search requests: %llu\n", st->req_search);
	printf("bind requests: %llu\n", st->req_bind);
	printf("modify requests: %llu\n", st->req_mod);
	printf("timeouts: %llu\n", st->timeouts);
	printf("unindexed searches: %llu\n", st->unindexed);
	printf("active connections: %u\n", st->conns);
	printf("active searches: %u\n", st->searches);
}

#define ZDIV(t,n)	((n) == 0 ? 0 : (float)(t) / (n))

void
show_dbstats(const char *prefix, struct btree_stat *st)
{
	printf("%s timestamp: %s", prefix, ctime(&st->created_at));
	printf("%s page size: %u\n", prefix, st->psize);
	printf("%s depth: %u\n", prefix, st->depth);
	printf("%s revisions: %u\n", prefix, st->revisions);
	printf("%s entries: %llu\n", prefix, st->entries);
	printf("%s branch/leaf/overflow pages: %u/%u/%u\n",
	    prefix, st->branch_pages, st->leaf_pages, st->overflow_pages);

	printf("%s cache size: %u of %u (%.1f%% full)\n", prefix,
	    st->cache_size, st->max_cache,
	    100 * ZDIV(st->cache_size, st->max_cache));
	printf("%s page reads: %llu\n", prefix, st->reads);
	printf("%s cache hits: %llu (%.1f%%)\n", prefix, st->hits,
	    100 * ZDIV(st->hits, (st->hits + st->reads)));
}

void
show_nsstats(struct imsg *imsg)
{
	struct ns_stat		*nss;

	nss = imsg->data;

	printf("\nsuffix: %s\n", nss->suffix);
	show_dbstats("data", &nss->data_stat);
	show_dbstats("indx", &nss->indx_stat);
}

