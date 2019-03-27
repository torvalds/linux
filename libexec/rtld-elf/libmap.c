/*
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "rtld.h"
#include "libmap.h"
#include "paths.h"

TAILQ_HEAD(lm_list, lm);
struct lm {
	char *f;
	char *t;
	TAILQ_ENTRY(lm)	lm_link;
};

static TAILQ_HEAD(lmp_list, lmp) lmp_head = TAILQ_HEAD_INITIALIZER(lmp_head);
struct lmp {
	char *p;
	enum { T_EXACT=0, T_BASENAME, T_DIRECTORY } type;
	struct lm_list lml;
	TAILQ_ENTRY(lmp) lmp_link;
};

static TAILQ_HEAD(lmc_list, lmc) lmc_head = TAILQ_HEAD_INITIALIZER(lmc_head);
struct lmc {
	char *path;
	dev_t dev;
	ino_t ino;
	TAILQ_ENTRY(lmc) next;
};

static int lm_count;

static void lmc_parse(char *, size_t);
static void lmc_parse_file(const char *);
static void lmc_parse_dir(const char *);
static void lm_add(const char *, const char *, const char *);
static void lm_free(struct lm_list *);
static char *lml_find(struct lm_list *, const char *);
static struct lm_list *lmp_find(const char *);
static struct lm_list *lmp_init(char *);
static const char *quickbasename(const char *);

#define	iseol(c)	(((c) == '#') || ((c) == '\0') || \
			 ((c) == '\n') || ((c) == '\r'))

/*
 * Do not use ctype.h macros, which rely on working TLS.  It is
 * too early to have thread-local variables functional.
 */
#define	rtld_isspace(c)	((c) == ' ' || (c) == '\t')

int
lm_init(char *libmap_override)
{
	char *p;

	dbg("lm_init(\"%s\")", libmap_override);
	TAILQ_INIT(&lmp_head);

	lmc_parse_file(ld_path_libmap_conf);

	if (libmap_override) {
		/*
		 * Do some character replacement to make $LDLIBMAP look
		 * like a text file, then parse it.
		 */
		libmap_override = xstrdup(libmap_override);
		for (p = libmap_override; *p; p++) {
			switch (*p) {
			case '=':
				*p = ' ';
				break;
			case ',':
				*p = '\n';
				break;
			}
		}
		lmc_parse(libmap_override, p - libmap_override);
		free(libmap_override);
	}

	return (lm_count == 0);
}

static void
lmc_parse_file(const char *path)
{
	struct lmc *p;
	char *lm_map;
	struct stat st;
	ssize_t retval;
	int fd;

	TAILQ_FOREACH(p, &lmc_head, next) {
		if (strcmp(p->path, path) == 0)
			return;
	}

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd == -1) {
		dbg("lm_parse_file: open(\"%s\") failed, %s", path,
		    rtld_strerror(errno));
		return;
	}
	if (fstat(fd, &st) == -1) {
		close(fd);
		dbg("lm_parse_file: fstat(\"%s\") failed, %s", path,
		    rtld_strerror(errno));
		return;
	}

	TAILQ_FOREACH(p, &lmc_head, next) {
		if (p->dev == st.st_dev && p->ino == st.st_ino) {
			close(fd);
			return;
		}
	}

	lm_map = xmalloc(st.st_size);
	retval = read(fd, lm_map, st.st_size);
	if (retval != st.st_size) {
		close(fd);
		free(lm_map);
		dbg("lm_parse_file: read(\"%s\") failed, %s", path,
		    rtld_strerror(errno));
		return;
	}
	close(fd);
	p = xmalloc(sizeof(struct lmc));
	p->path = xstrdup(path);
	p->dev = st.st_dev;
	p->ino = st.st_ino;
	TAILQ_INSERT_HEAD(&lmc_head, p, next);
	lmc_parse(lm_map, st.st_size);
	free(lm_map);
}

static void
lmc_parse_dir(const char *idir)
{
	DIR *d;
	struct dirent *dp;
	struct lmc *p;
	char conffile[MAXPATHLEN];
	char *ext;

	TAILQ_FOREACH(p, &lmc_head, next) {
		if (strcmp(p->path, idir) == 0)
			return;
	}
	d = opendir(idir);
	if (d == NULL)
		return;

	p = xmalloc(sizeof(struct lmc));
	p->path = xstrdup(idir);
	p->dev = NODEV;
	p->ino = 0;
	TAILQ_INSERT_HEAD(&lmc_head, p, next);

	while ((dp = readdir(d)) != NULL) {
		if (dp->d_ino == 0)
			continue;
		if (dp->d_type != DT_REG)
			continue;
		ext = strrchr(dp->d_name, '.');
		if (ext == NULL)
			continue;
		if (strcmp(ext, ".conf") != 0)
			continue;
		if (strlcpy(conffile, idir, MAXPATHLEN) >= MAXPATHLEN)
			continue; /* too long */
		if (strlcat(conffile, "/", MAXPATHLEN) >= MAXPATHLEN)
			continue; /* too long */
		if (strlcat(conffile, dp->d_name, MAXPATHLEN) >= MAXPATHLEN)
			continue; /* too long */
		lmc_parse_file(conffile);
	}
	closedir(d);
}

static void
lmc_parse(char *lm_p, size_t lm_len)
{
	char *cp, *f, *t, *c, *p;
	char prog[MAXPATHLEN];
	/* allow includedir + full length path */
	char line[MAXPATHLEN + 13];
	size_t cnt, i;

	cnt = 0;
	p = NULL;
	while (cnt < lm_len) {
		i = 0;
		while (cnt < lm_len && lm_p[cnt] != '\n' &&
		    i < sizeof(line) - 1) {
			line[i] = lm_p[cnt];
			cnt++;
			i++;
		}
		line[i] = '\0';
		while (cnt < lm_len && lm_p[cnt] != '\n')
			cnt++;
		/* skip over nl */
		cnt++;

		cp = &line[0];
		t = f = c = NULL;

		/* Skip over leading space */
		while (rtld_isspace(*cp))
			cp++;

		/* Found a comment or EOL */
		if (iseol(*cp))
			continue;

		/* Found a constraint selector */
		if (*cp == '[') {
			cp++;

			/* Skip leading space */
			while (rtld_isspace(*cp))
				cp++;

			/* Found comment, EOL or end of selector */
			if  (iseol(*cp) || *cp == ']')
				continue;

			c = cp++;
			/* Skip to end of word */
			while (!rtld_isspace(*cp) && !iseol(*cp) && *cp != ']')
				cp++;

			/* Skip and zero out trailing space */
			while (rtld_isspace(*cp))
				*cp++ = '\0';

			/* Check if there is a closing brace */
			if (*cp != ']')
				continue;

			/* Terminate string if there was no trailing space */
			*cp++ = '\0';

			/*
			 * There should be nothing except whitespace or comment
			  from this point to the end of the line.
			 */
			while (rtld_isspace(*cp))
				cp++;
			if (!iseol(*cp))
				continue;

			if (strlcpy(prog, c, sizeof prog) >= sizeof prog)
				continue;
			p = prog;
			continue;
		}

		/* Parse the 'from' candidate. */
		f = cp++;
		while (!rtld_isspace(*cp) && !iseol(*cp))
			cp++;

		/* Skip and zero out the trailing whitespace */
		while (rtld_isspace(*cp))
			*cp++ = '\0';

		/* Found a comment or EOL */
		if (iseol(*cp))
			continue;

		/* Parse 'to' mapping */
		t = cp++;
		while (!rtld_isspace(*cp) && !iseol(*cp))
			cp++;

		/* Skip and zero out the trailing whitespace */
		while (rtld_isspace(*cp))
			*cp++ = '\0';

		/* Should be no extra tokens at this point */
		if (!iseol(*cp))
			continue;

		*cp = '\0';
		if (strcmp(f, "includedir") == 0)
			lmc_parse_dir(t);
		else if (strcmp(f, "include") == 0)
			lmc_parse_file(t);
		else
			lm_add(p, f, t);
	}
}

static void
lm_free(struct lm_list *lml)
{
	struct lm *lm;

	dbg("%s(%p)", __func__, lml);

	while (!TAILQ_EMPTY(lml)) {
		lm = TAILQ_FIRST(lml);
		TAILQ_REMOVE(lml, lm, lm_link);
		free(lm->f);
		free(lm->t);
		free(lm);
	}
}

void
lm_fini(void)
{
	struct lmp *lmp;
	struct lmc *p;

	dbg("%s()", __func__);

	while (!TAILQ_EMPTY(&lmc_head)) {
		p = TAILQ_FIRST(&lmc_head);
		TAILQ_REMOVE(&lmc_head, p, next);
		free(p->path);
		free(p);
	}

	while (!TAILQ_EMPTY(&lmp_head)) {
		lmp = TAILQ_FIRST(&lmp_head);
		TAILQ_REMOVE(&lmp_head, lmp, lmp_link);
		free(lmp->p);
		lm_free(&lmp->lml);
		free(lmp);
	}
}

static void
lm_add(const char *p, const char *f, const char *t)
{
	struct lm_list *lml;
	struct lm *lm;
	const char *t1;

	if (p == NULL)
		p = "$DEFAULT$";

	dbg("%s(\"%s\", \"%s\", \"%s\")", __func__, p, f, t);

	if ((lml = lmp_find(p)) == NULL)
		lml = lmp_init(xstrdup(p));

	t1 = lml_find(lml, f);
	if (t1 == NULL || strcmp(t1, t) != 0) {
		lm = xmalloc(sizeof(struct lm));
		lm->f = xstrdup(f);
		lm->t = xstrdup(t);
		TAILQ_INSERT_HEAD(lml, lm, lm_link);
		lm_count++;
	}
}

char *
lm_find(const char *p, const char *f)
{
	struct lm_list *lml;
	char *t;

	dbg("%s(\"%s\", \"%s\")", __func__, p, f);

	if (p != NULL && (lml = lmp_find(p)) != NULL) {
		t = lml_find(lml, f);
		if (t != NULL) {
			/*
			 * Add a global mapping if we have
			 * a successful constrained match.
			 */
			lm_add(NULL, f, t);
			return (t);
		}
	}
	lml = lmp_find("$DEFAULT$");
	if (lml != NULL)
		return (lml_find(lml, f));
	return (NULL);
}

/*
 * Given a libmap translation list and a library name, return the
 * replacement library, or NULL.
 */
char *
lm_findn(const char *p, const char *f, const size_t n)
{
	char pathbuf[64], *s, *t;

	if (n < sizeof(pathbuf) - 1)
		s = pathbuf;
	else
		s = xmalloc(n + 1);
	memcpy(s, f, n);
	s[n] = '\0';
	t = lm_find(p, s);
	if (s != pathbuf)
		free(s);
	return (t);
}

static char *
lml_find(struct lm_list *lmh, const char *f)
{
	struct lm *lm;

	dbg("%s(%p, \"%s\")", __func__, lmh, f);

	TAILQ_FOREACH(lm, lmh, lm_link) {
		if (strcmp(f, lm->f) == 0)
			return (lm->t);
	}
	return (NULL);
}

/*
 * Given an executable name, return a pointer to the translation list or
 * NULL if no matches.
 */
static struct lm_list *
lmp_find(const char *n)
{
	struct lmp *lmp;

	dbg("%s(\"%s\")", __func__, n);

	TAILQ_FOREACH(lmp, &lmp_head, lmp_link) {
		if ((lmp->type == T_EXACT && strcmp(n, lmp->p) == 0) ||
		    (lmp->type == T_DIRECTORY && strncmp(n, lmp->p,
		    strlen(lmp->p)) == 0) ||
		    (lmp->type == T_BASENAME && strcmp(quickbasename(n),
		    lmp->p) == 0))
			return (&lmp->lml);
	}
	return (NULL);
}

static struct lm_list *
lmp_init(char *n)
{
	struct lmp *lmp;

	dbg("%s(\"%s\")", __func__, n);

	lmp = xmalloc(sizeof(struct lmp));
	lmp->p = n;
	if (n[strlen(n) - 1] == '/')
		lmp->type = T_DIRECTORY;
	else if (strchr(n,'/') == NULL)
		lmp->type = T_BASENAME;
	else
		lmp->type = T_EXACT;
	TAILQ_INIT(&lmp->lml);
	TAILQ_INSERT_HEAD(&lmp_head, lmp, lmp_link);

	return (&lmp->lml);
}

/*
 * libc basename is overkill.  Return a pointer to the character after
 * the last /, or the original string if there are no slashes.
 */
static const char *
quickbasename(const char *path)
{
	const char *p;

	for (p = path; *path != '\0'; path++) {
		if (*path == '/')
			p = path + 1;
	}
	return (p);
}
