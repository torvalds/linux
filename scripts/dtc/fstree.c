// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2005.
 */

#include "dtc.h"

#include <dirent.h>
#include <sys/stat.h>

static struct node *read_fstree(const char *dirname)
{
	DIR *d;
	struct dirent *de;
	struct stat st;
	struct node *tree;

	d = opendir(dirname);
	if (!d)
		die("Couldn't opendir() \"%s\": %s\n", dirname, strerror(errno));

	tree = build_node(NULL, NULL, NULL);

	while ((de = readdir(d)) != NULL) {
		char *tmpname;

		if (streq(de->d_name, ".")
		    || streq(de->d_name, ".."))
			continue;

		tmpname = join_path(dirname, de->d_name);

		if (stat(tmpname, &st) < 0)
			die("stat(%s): %s\n", tmpname, strerror(errno));

		if (S_ISREG(st.st_mode)) {
			struct property *prop;
			FILE *pfile;

			pfile = fopen(tmpname, "rb");
			if (! pfile) {
				fprintf(stderr,
					"WARNING: Cannot open %s: %s\n",
					tmpname, strerror(errno));
			} else {
				prop = build_property(xstrdup(de->d_name),
						      data_copy_file(pfile,
								     st.st_size),
						      NULL);
				add_property(tree, prop);
				fclose(pfile);
			}
		} else if (S_ISDIR(st.st_mode)) {
			struct node *newchild;

			newchild = read_fstree(tmpname);
			newchild = name_node(newchild, xstrdup(de->d_name));
			add_child(tree, newchild);
		}

		free(tmpname);
	}

	closedir(d);
	return tree;
}

struct dt_info *dt_from_fs(const char *dirname)
{
	struct node *tree;

	tree = read_fstree(dirname);
	tree = name_node(tree, "");

	return build_dt_info(DTSF_V1, NULL, tree, guess_boot_cpuid(tree));
}
