/*	$OpenBSD: fnm_test.c,v 1.3 2019/01/25 00:19:26 millert Exp $	*/

/*
 * Public domain, 2008, Todd C. Miller <millert@openbsd.org>
 */

#include <err.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <util.h>

int
main(int argc, char **argv)
{
	FILE *fp = stdin;
	char pattern[1024], string[1024];
	char *line;
	const char delim[3] = {'\0', '\0', '#'};
	int errors = 0, flags, got, want;

	if (argc > 1) {
		if ((fp = fopen(argv[1], "r")) == NULL)
			err(1, "%s", argv[1]);
	}

	/*
	 * Read in test file, which is formatted thusly:
	 *
	 * pattern string flags expected_result
	 *
	 * lines starting with '#' are comments
	 */
	for (;;) {
		line = fparseln(fp, NULL, NULL, delim, 0);
		if (!line)
			break;
		got = sscanf(line, "%s %s 0x%x %d", pattern, string, &flags,
		    &want);
		if (got == EOF) {
			free(line);
			break;
		}
		if (pattern[0] == '#') {
			free(line);
			continue;
		}
		if (got == 4) {
			got = fnmatch(pattern, string, flags);
			if (got != want) {
				warnx("%s %s %d: want %d, got %d", pattern,
				    string, flags, want, got);
				errors++;
			}
		} else {
			warnx("unrecognized line '%s'\n", line);
			errors++;
		}
		free(line);
	}
	exit(errors);
}
