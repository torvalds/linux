/*
 * Copyright (c) 2007 Bret S. Lambert <blambert@gsipt.net>
 *
 * Public domain.
 */

#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <err.h>

int
main(void)
{
	char path[2 * PATH_MAX];
	char dname[128];
	const char *dir = "junk";
	const char *fname = "/file.name.ext";
	char *str;
	int i;

	/* Test normal functioning */
	strlcpy(path, "/", sizeof(path));
	strlcpy(dname, "/", sizeof(dname));
	strlcat(path, dir, sizeof(path)); 
	strlcat(dname, dir, sizeof(dname)); 
	strlcat(path, fname, sizeof(path));
	str = dirname(path);
	if (strcmp(str, dname) != 0)
		errx(1, "0: dirname(%s) = %s != %s", path, str, dname);

	/*
	 * There are four states that require special handling:
	 *
	 * 1) path is NULL
	 * 2) path is the empty string
	 * 3) path is composed entirely of slashes
	 * 4) the resulting name is larger than PATH_MAX
	 *
	 * The first two cases require that a pointer
	 * to the string "." be returned.
	 *
	 * The third case requires that a pointer
	 * to the string "/" be returned.
	 *
	 * The final case requires that NULL be returned
	 * and errno * be set to ENAMETOOLONG.
	 */
	/* Case 1 */
	str = dirname(NULL);
	if (strcmp(str, ".") != 0)
		errx(1, "1: dirname(NULL) = %s != .", str);

	/* Case 2 */
	strlcpy(path, "", sizeof(path));
	str = dirname(path);
	if (strcmp(str, ".") != 0)
		errx(1, "2: dirname(%s) = %s != .", path, str);

	/* Case 3 */
	for (i = 0; i < PATH_MAX - 1; i++)
		strlcat(path, "/", sizeof(path));	/* path cleared above */
	str = dirname(path);
	if (strcmp(str, "/") != 0)
		errx(1, "3: dirname(%s) = %s != /", path, str);

	/* Case 4 */
	strlcpy(path, "/", sizeof(path));		/* reset path */
	for (i = 0; i <= PATH_MAX; i += strlen(dir))
		strlcat(path, dir, sizeof(path));
	strlcat(path, fname, sizeof(path));
	str = dirname(path);
	if (str != NULL)
		errx(1, "4: dirname(%s) = %s != NULL", path, str);
	if (errno != ENAMETOOLONG)
		errx(1, "4: dirname(%s) sets errno to %d", path, errno);

	return (0);
}
