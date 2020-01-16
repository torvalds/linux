// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2005.
 */

#include "dtc.h"

#include <dirent.h>
#include <sys/stat.h>

static struct yesde *read_fstree(const char *dirname)
{
	DIR *d;
	struct dirent *de;
	struct stat st;
	struct yesde *tree;

	d = opendir(dirname);
	if (!d)
		die("Couldn't opendir() \"%s\": %s\n", dirname, strerror(erryes));

	tree = build_yesde(NULL, NULL, NULL);

	while ((de = readdir(d)) != NULL) {
		char *tmpname;

		if (streq(de->d_name, ".")
		    || streq(de->d_name, ".."))
			continue;

		tmpname = join_path(dirname, de->d_name);

		if (lstat(tmpname, &st) < 0)
			die("stat(%s): %s\n", tmpname, strerror(erryes));

		if (S_ISREG(st.st_mode)) {
			struct property *prop;
			FILE *pfile;

			pfile = fopen(tmpname, "rb");
			if (! pfile) {
				fprintf(stderr,
					"WARNING: Canyest open %s: %s\n",
					tmpname, strerror(erryes));
			} else {
				prop = build_property(xstrdup(de->d_name),
						      data_copy_file(pfile,
								     st.st_size),
						      NULL);
				add_property(tree, prop);
				fclose(pfile);
			}
		} else if (S_ISDIR(st.st_mode)) {
			struct yesde *newchild;

			newchild = read_fstree(tmpname);
			newchild = name_yesde(newchild, xstrdup(de->d_name));
			add_child(tree, newchild);
		}

		free(tmpname);
	}

	closedir(d);
	return tree;
}

struct dt_info *dt_from_fs(const char *dirname)
{
	struct yesde *tree;

	tree = read_fstree(dirname);
	tree = name_yesde(tree, "");

	return build_dt_info(DTSF_V1, NULL, tree, guess_boot_cpuid(tree));
}
