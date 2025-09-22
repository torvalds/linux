/*	$OpenBSD: globtest.c,v 1.4 2019/01/25 00:19:26 millert Exp $	*/

/*
 * Public domain, 2008, Todd C. Miller <millert@openbsd.org>
 */

#include <err.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RESULTS	256

struct gl_entry {
	int flags;
	int nresults;
	char pattern[1024];
	char *results[MAX_RESULTS];
	mode_t modes[MAX_RESULTS];
};

int test_glob(struct gl_entry *);

int
main(int argc, char **argv)
{
	FILE *fp = stdin;
	char *buf, *cp;
	int errors = 0, lineno, mode;
	struct gl_entry entry;
	size_t len;

	if (argc > 1) {
		if ((fp = fopen(argv[1], "r")) == NULL)
			err(1, "%s", argv[1]);
	}

	/*
	 * Read in test file, which is formatted thusly:
	 *
	 * [pattern] <flags>
	 * result1 [mode]
	 * result2 [mode]
	 * result3 [mode]
	 * ...
	 *
	 */
	lineno = 0;
	memset(&entry, 0, sizeof(entry));
	while ((buf = fgetln(fp, &len)) != NULL) {
		lineno++;
		if (buf[len - 1] != '\n')
			errx(1, "missing newline at EOF");
		buf[--len] = '\0';
		if (len == 0)
			continue; /* blank line */

		if (buf[0] == '[') {
			/* check previous pattern */
			if (entry.pattern[0])
				errors += test_glob(&entry);

			/* start new entry */
			if ((cp = strrchr(buf + 1, ']')) == NULL)
				errx(1, "invalid entry on line %d", lineno);
			len = cp - buf - 1;
			if (len >= sizeof(entry.pattern))
				errx(1, "pattern too big on line %d", lineno);
			memcpy(entry.pattern, buf + 1, len);
			entry.pattern[len] = '\0';

			buf = cp + 2;
			if (*buf++ != '<')
				errx(1, "invalid entry on line %d", lineno);
			if ((cp = strchr(buf, '>')) == NULL)
				errx(1, "invalid entry on line %d", lineno);
			entry.flags = (int)strtol(buf, &cp, 0);
			if (*cp != '>' || entry.flags < 0 || entry.flags > 0x4000)
				errx(1, "invalid flags: %s", buf);
			entry.nresults = 0;
			continue;
		}
		if (!entry.pattern[0])
			errx(1, "missing entry on line %d", lineno);

		if (entry.nresults + 1 > MAX_RESULTS) {
			errx(1, "too many results for %s, max %d",
			    entry.pattern, MAX_RESULTS);
		}
		if ((cp = strchr(buf, ' ')) != NULL) {
			*cp++ = '\0';
			mode = strtol(cp, NULL, 8);
		} else
			mode = -1;
		entry.modes[entry.nresults] = (mode_t)mode;
		entry.results[entry.nresults++] = strdup(buf);
	}
	if (entry.pattern[0])
		errors += test_glob(&entry); /* test last pattern */
	exit(errors);
}

int test_glob(struct gl_entry *entry)
{
	glob_t gl;
	int i = 0;

	if (glob(entry->pattern, entry->flags, NULL, &gl) != 0)
		errx(1, "glob failed: %s", entry->pattern);

	if (gl.gl_matchc != entry->nresults)
		goto mismatch;

	for (i = 0; i < gl.gl_matchc; i++) {
		if (strcmp(gl.gl_pathv[i], entry->results[i]) != 0)
			goto mismatch;
		if ((entry->flags & GLOB_KEEPSTAT) != 0) {
			if (entry->modes[i] == -1 ||
			    gl.gl_statv[i] == NULL ||
			    entry->modes[i] != gl.gl_statv[i]->st_mode)
			goto badmode;
		}
		free(entry->results[i]);
	}
	return (0);
 badmode:
	warnx("mismatch mode for pattern %s, flags 0x%x, file \"%s\" "
	    "(found %07o, expected %07o)", entry->pattern, entry->flags,
	    gl.gl_pathv[i], gl.gl_statv[i] ? gl.gl_statv[i]->st_mode : 0,
	    entry->modes[i]);
	goto cleanup;
 mismatch:
	warnx("mismatch for pattern %s, flags 0x%x "
	    "(found \"%s\", expected \"%s\")", entry->pattern, entry->flags,
	    gl.gl_pathv[i], entry->results[i]);
 cleanup:
	while (i < gl.gl_matchc) {
		free(entry->results[i++]);
	}
	return (1);
}
