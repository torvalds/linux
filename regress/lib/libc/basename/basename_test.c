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

int
main(void)
{
	char path[2 * PATH_MAX];
	const char *dir = "junk/";
	const char *fname = "file.name.ext";
	char *str;
	int i;

	/* Test normal functioning */
	strlcpy(path, "/", sizeof(path));
	strlcat(path, dir, sizeof(path)); 
	strlcat(path, fname, sizeof(path));
	str = basename(path);
	if (strcmp(str, fname) != 0)
		goto fail;

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
	str = basename(NULL);
	if (strcmp(str, ".") != 0)
		goto fail;

	/* Case 2 */
	strlcpy(path, "", sizeof(path));
	str = basename(path);
	if (strcmp(str, ".") != 0)
		goto fail;

	/* Case 3 */
	for (i = 0; i < PATH_MAX - 1; i++)
		strlcat(path, "/", sizeof(path));	/* path cleared above */
	str = basename(path);
	if (strcmp(str, "/") != 0)
		goto fail;

	/* Case 4 */
	strlcpy(path, "/", sizeof(path));
	strlcat(path, dir, sizeof(path));
	for (i = 0; i <= PATH_MAX; i += sizeof(fname))
		strlcat(path, fname, sizeof(path));
	str = basename(path);
	if (str != NULL || errno != ENAMETOOLONG)
		goto fail;

	return (0);
fail:
	return (1);
}
