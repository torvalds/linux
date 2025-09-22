/*	$OpenBSD: makemap.c,v 1.77 2024/05/07 12:10:06 op Exp $	*/

/*
 * Copyright (c) 2008 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2008-2009 Jacek Masiulaniec <jacekm@dobremiasto.net>
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

#include <ctype.h>
#include <db.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <util.h>

#include "smtpd.h"
#include "log.h"

#define	PATH_ALIASES	"/etc/mail/aliases"

static void	 usage(void);
static int	 parse_map(DB *, int *, char *);
static int	 add_mapentry(DB *, int *, char *, char *, size_t);
static int	 add_setentry(DB *, int *, char *, size_t);
static int	 make_plain(DBT *, char *);
static int	 make_aliases(DBT *, char *);
static char	*conf_aliases(char *);
static int	 dump_db(const char *, DBTYPE);

char		*source;
static int	 mode;

enum output_type {
	T_PLAIN,
	T_ALIASES,
	T_SET
} type;

/*
 * Stub functions so that makemap compiles using minimum object files.
 */
int
fork_proc_backend(const char *backend, const char *conf, const char *procname,
    int do_stdout)
{
	return (-1);
}

int
makemap(int prog_mode, int argc, char *argv[])
{
	struct stat	 sb;
	char		 dbname[PATH_MAX];
	DB		*db;
	const char	*opts;
	char		*conf, *oflag = NULL;
	int		 ch, dbputs = 0, Uflag = 0;
	DBTYPE		 dbtype = DB_HASH;
	char		*p;
	gid_t		 gid;
	int		 fd = -1;

	gid = getgid();
	if (setresgid(gid, gid, gid) == -1)
		err(1, "setresgid");

	if ((env = config_default()) == NULL)
		err(1, NULL);

	log_init(1, LOG_MAIL);

	mode = prog_mode;
	conf = CONF_FILE;
	type = T_PLAIN;
	opts = "b:C:d:ho:O:t:U";
	if (mode == P_NEWALIASES)
		opts = "f:h";

	while ((ch = getopt(argc, argv, opts)) != -1) {
		switch (ch) {
		case 'b':
			if (optarg && strcmp(optarg, "i") == 0)
				mode = P_NEWALIASES;
			break;
		case 'C':
			break; /* for compatibility */
		case 'd':
			if (strcmp(optarg, "hash") == 0)
				dbtype = DB_HASH;
			else if (strcmp(optarg, "btree") == 0)
				dbtype = DB_BTREE;
			else
				errx(1, "unsupported DB type '%s'", optarg);
			break;
		case 'f':
			conf = optarg;
			break;
		case 'o':
			oflag = optarg;
			break;
		case 'O':
			if (strncmp(optarg, "AliasFile=", 10) != 0)
				break;
			type = T_ALIASES;
			p = strchr(optarg, '=');
			source = ++p;
			break;
		case 't':
			if (strcmp(optarg, "aliases") == 0)
				type = T_ALIASES;
			else if (strcmp(optarg, "set") == 0)
				type = T_SET;
			else
				errx(1, "unsupported type '%s'", optarg);
			break;
		case 'U':
			Uflag = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/* sendmail-compat makemap ... re-execute using proper interface */
	if (argc == 2) {
		if (oflag)
			usage();

		p = strstr(argv[1], ".db");
		if (p == NULL || strcmp(p, ".db") != 0) {
			if (!bsnprintf(dbname, sizeof dbname, "%s.db",
				argv[1]))
				errx(1, "database name too long");
		}
		else {
			if (strlcpy(dbname, argv[1], sizeof dbname)
			    >= sizeof dbname)
				errx(1, "database name too long");
		}

		execl(PATH_MAKEMAP, "makemap", "-d", argv[0], "-o", dbname,
		    "-", (char *)NULL);
		err(1, "execl");
	}

	if (mode == P_NEWALIASES) {
		if (geteuid())
			errx(1, "need root privileges");
		if (argc != 0)
			usage();
		type = T_ALIASES;
		if (source == NULL)
			source = conf_aliases(conf);
	} else {
		if (argc != 1)
			usage();
		source = argv[0];
	}

	if (Uflag)
		return dump_db(source, dbtype);

	if (oflag == NULL && asprintf(&oflag, "%s.db", source) == -1)
		err(1, "asprintf");

	if (strcmp(source, "-") != 0)
		if (stat(source, &sb) == -1)
			err(1, "stat: %s", source);

	if (!bsnprintf(dbname, sizeof(dbname), "%s.XXXXXXXXXXX", oflag))
		errx(1, "path too long");
	if ((fd = mkstemp(dbname)) == -1)
		err(1, "mkstemp");

	db = dbopen(dbname, O_TRUNC|O_RDWR, 0644, dbtype, NULL);
	if (db == NULL) {
		warn("dbopen: %s", dbname);
		goto bad;
	}

	if (strcmp(source, "-") != 0)
		if (fchmod(db->fd(db), sb.st_mode) == -1 ||
		    fchown(db->fd(db), sb.st_uid, sb.st_gid) == -1) {
			warn("couldn't carry ownership and perms to %s",
			    dbname);
			goto bad;
		}

	if (!parse_map(db, &dbputs, source))
		goto bad;

	if (db->close(db) == -1) {
		warn("dbclose: %s", dbname);
		goto bad;
	}

	/* force to disk before renaming over an existing file */
	if (fsync(fd) == -1) {
		warn("fsync: %s", dbname);
		goto bad;
	}
	if (close(fd) == -1) {
		fd = -1;
		warn("close: %s", dbname);
		goto bad;
	}
	fd = -1;

	if (rename(dbname, oflag) == -1) {
		warn("rename");
		goto bad;
	}

	if (mode == P_NEWALIASES)
		printf("%s: %d aliases\n", source, dbputs);
	else if (dbputs == 0)
		warnx("warning: empty map created: %s", oflag);

	return 0;
bad:
	if (fd != -1)
		close(fd);
	unlink(dbname);
	return 1;
}

static int
parse_map(DB *db, int *dbputs, char *filename)
{
	FILE	*fp;
	char	*key, *val, *line = NULL;
	size_t	 linesize = 0;
	size_t	 lineno = 0;
	int	 malformed, table_type, r;

	if (strcmp(filename, "-") == 0)
		fp = fdopen(0, "r");
	else
		fp = fopen(filename, "r");
	if (fp == NULL) {
		warn("%s", filename);
		return 0;
	}

	if (!isatty(fileno(fp)) && flock(fileno(fp), LOCK_SH|LOCK_NB) == -1) {
		if (errno == EWOULDBLOCK)
			warnx("%s is locked", filename);
		else
			warn("%s: flock", filename);
		fclose(fp);
		return 0;
	}

	table_type = (type == T_SET) ? T_LIST : T_HASH;
	while (parse_table_line(fp, &line, &linesize, &table_type,
	    &key, &val, &malformed) != -1) {
		lineno++;
		if (malformed) {
			warnx("%s:%zd: invalid entry", source, lineno);
			free(line);
			fclose(fp);
			return 0;
		}
		if (key == NULL)
			continue;

		switch (type) {
		case T_PLAIN:
		case T_ALIASES:
			r = add_mapentry(db, dbputs, key, val, lineno);
			break;
		case T_SET:
			r = add_setentry(db, dbputs, key, lineno);
			break;
		}

		if (!r) {
			free(line);
			fclose(fp);
			return 0;
		}
	}

	free(line);
	fclose(fp);
	return 1;
}

static int
add_mapentry(DB *db, int *dbputs, char *keyp, char *valp, size_t lineno)
{
	DBT	 key;
	DBT	 val;

	/* Check for dups. */
	key.data = keyp;
	key.size = strlen(keyp) + 1;

	xlowercase(key.data, key.data, strlen(key.data) + 1);
	if (db->get(db, &key, &val, 0) == 0) {
		warnx("%s:%zd: duplicate entry for %s", source, lineno, keyp);
		return 0;
	}

	if (type == T_PLAIN) {
		if (!make_plain(&val, valp))
			goto bad;
	}
	else if (type == T_ALIASES) {
		if (!make_aliases(&val, valp))
			goto bad;
	}

	if (db->put(db, &key, &val, 0) == -1) {
		warn("dbput");
		return 0;
	}

	(*dbputs)++;

	free(val.data);

	return 1;

bad:
	warnx("%s:%zd: invalid entry", source, lineno);
	return 0;
}

static int
add_setentry(DB *db, int *dbputs, char *keyp, size_t lineno)
{
	DBT	 key;
	DBT	 val;

	val.data  = "<set>";
	val.size = strlen(val.data) + 1;

	/* Check for dups. */
	key.data = keyp;
	key.size = strlen(keyp) + 1;
	xlowercase(key.data, key.data, strlen(key.data) + 1);
	if (db->get(db, &key, &val, 0) == 0) {
		warnx("%s:%zd: duplicate entry for %s", source, lineno, keyp);
		return 0;
	}

	if (db->put(db, &key, &val, 0) == -1) {
		warn("dbput");
		return 0;
	}

	(*dbputs)++;

	return 1;
}

static int
make_plain(DBT *val, char *text)
{
	val->data = xstrdup(text);
	val->size = strlen(text) + 1;

	return (val->size);
}

static int
make_aliases(DBT *val, char *text)
{
	struct expandnode	xn;
	char		       *subrcpt;
	char		       *origtext;

	val->data = NULL;
	val->size = 0;

	origtext = xstrdup(text);

	while ((subrcpt = strsep(&text, ",")) != NULL) {
		/* subrcpt: strip initial and trailing whitespace. */
		subrcpt = strip(subrcpt);
		if (*subrcpt == '\0')
			goto error;

		if (!text_to_expandnode(&xn, subrcpt))
			goto error;
	}

	val->data = origtext;
	val->size = strlen(origtext) + 1;
	return (val->size);

error:
	free(origtext);

	return 0;
}

static char *
conf_aliases(char *cfgpath)
{
	struct table	*table;
	char		*path;
	char		*p;

	if (parse_config(env, cfgpath, 0))
		exit(1);

	table = table_find(env, "aliases");
	if (table == NULL)
		return (PATH_ALIASES);

	path = xstrdup(table->t_config);
	p = strstr(path, ".db");
	if (p == NULL || strcmp(p, ".db") != 0) {
		return (path);
	}
	*p = '\0';
	return (path);
}

static int
dump_db(const char *dbname, DBTYPE dbtype)
{
	DB	*db;
	DBT	 key, val;
	char	*keystr, *valstr;
	int	 r;

	db = dbopen(dbname, O_RDONLY, 0644, dbtype, NULL);
	if (db == NULL)
		err(1, "dbopen: %s", dbname);

	for (r = db->seq(db, &key, &val, R_FIRST); r == 0;
	    r = db->seq(db, &key, &val, R_NEXT)) {
		keystr = key.data;
		valstr = val.data;
		if (keystr[key.size - 1] == '\0')
			key.size--;
		if (valstr[val.size - 1] == '\0')
			val.size--;
		printf("%.*s\t%.*s\n", (int)key.size, keystr,
		    (int)val.size, valstr);
	}
	if (r == -1)
		err(1, "db->seq: %s", dbname);

	if (db->close(db) == -1)
		err(1, "dbclose: %s", dbname);

	return 0;
}

static void
usage(void)
{
	if (mode == P_NEWALIASES)
		fprintf(stderr, "usage: newaliases [-f file]\n");
	else
		fprintf(stderr, "usage: makemap [-U] [-d dbtype] [-o dbfile] "
		    "[-t type] file\n");
	exit(1);
}
