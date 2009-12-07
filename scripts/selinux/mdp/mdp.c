/*
 *
 * mdp - make dummy policy
 *
 * When pointed at a kernel tree, builds a dummy policy for that kernel
 * with exactly one type with full rights to itself.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2006
 *
 * Authors: Serge E. Hallyn <serue@us.ibm.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "flask.h"

static void usage(char *name)
{
	printf("usage: %s [-m] policy_file context_file\n", name);
	exit(1);
}

static void find_common_name(char *cname, char *dest, int len)
{
	char *start, *end;

	start = strchr(cname, '_')+1;
	end = strchr(start, '_');
	if (!start || !end || start-cname > len || end-start > len) {
		printf("Error with commons defines\n");
		exit(1);
	}
	strncpy(dest, start, end-start);
	dest[end-start] = '\0';
}

#define S_(x) x,
static char *classlist[] = {
#include "class_to_string.h"
	NULL
};
#undef S_

#include "initial_sid_to_string.h"

#define TB_(x) char *x[] = {
#define TE_(x) NULL };
#define S_(x) x,
#include "common_perm_to_string.h"
#undef TB_
#undef TE_
#undef S_

struct common {
	char *cname;
	char **perms;
};
struct common common[] = {
#define TB_(x) { #x, x },
#define S_(x)
#define TE_(x)
#include "common_perm_to_string.h"
#undef TB_
#undef TE_
#undef S_
};

#define S_(x, y, z) {x, #y},
struct av_inherit {
	int class;
	char *common;
};
struct av_inherit av_inherit[] = {
#include "av_inherit.h"
};
#undef S_

#include "av_permissions.h"
#define S_(x, y, z) {x, y, z},
struct av_perms {
	int class;
	int perm_i;
	char *perm_s;
};
struct av_perms av_perms[] = {
#include "av_perm_to_string.h"
};
#undef S_

int main(int argc, char *argv[])
{
	int i, j, mls = 0;
	char **arg, *polout, *ctxout;
	int classlist_len, initial_sid_to_string_len;
	FILE *fout;

	if (argc < 3)
		usage(argv[0]);
	arg = argv+1;
	if (argc==4 && strcmp(argv[1], "-m") == 0) {
		mls = 1;
		arg++;
	}
	polout = *arg++;
	ctxout = *arg;

	fout = fopen(polout, "w");
	if (!fout) {
		printf("Could not open %s for writing\n", polout);
		usage(argv[0]);
	}

	classlist_len = sizeof(classlist) / sizeof(char *);
	/* print out the classes */
	for (i=1; i < classlist_len; i++) {
		if(classlist[i])
			fprintf(fout, "class %s\n", classlist[i]);
		else
			fprintf(fout, "class user%d\n", i);
	}
	fprintf(fout, "\n");

	initial_sid_to_string_len = sizeof(initial_sid_to_string) / sizeof (char *);
	/* print out the sids */
	for (i=1; i < initial_sid_to_string_len; i++)
		fprintf(fout, "sid %s\n", initial_sid_to_string[i]);
	fprintf(fout, "\n");

	/* print out the commons */
	for (i=0; i< sizeof(common)/sizeof(struct common); i++) {
		char cname[101];
		find_common_name(common[i].cname, cname, 100);
		cname[100] = '\0';
		fprintf(fout, "common %s\n{\n", cname);
		for (j=0; common[i].perms[j]; j++)
			fprintf(fout, "\t%s\n", common[i].perms[j]);
		fprintf(fout, "}\n\n");
	}
	fprintf(fout, "\n");

	/* print out the class permissions */
	for (i=1; i < classlist_len; i++) {
		if (classlist[i]) {
			int firstperm = -1, numperms = 0;

			fprintf(fout, "class %s\n", classlist[i]);
			/* does it inherit from a common? */
			for (j=0; j < sizeof(av_inherit)/sizeof(struct av_inherit); j++)
				if (av_inherit[j].class == i)
					fprintf(fout, "inherits %s\n", av_inherit[j].common);

			for (j=0; j < sizeof(av_perms)/sizeof(struct av_perms); j++) {
				if (av_perms[j].class == i) {
					if (firstperm == -1)
						firstperm = j;
					numperms++;
				}
			}
			if (!numperms) {
				fprintf(fout, "\n");
				continue;
			}

			fprintf(fout, "{\n");
			/* print out the av_perms */
			for (j=0; j < numperms; j++) {
				fprintf(fout, "\t%s\n", av_perms[firstperm+j].perm_s);
			}
			fprintf(fout, "}\n\n");
		}
	}
	fprintf(fout, "\n");

	/* NOW PRINT OUT MLS STUFF */
	if (mls) {
		printf("MLS not yet implemented\n");
		exit(1);
	}

	/* types, roles, and allows */
	fprintf(fout, "type base_t;\n");
	fprintf(fout, "role base_r types { base_t };\n");
	for (i=1; i < classlist_len; i++) {
		if (classlist[i])
			fprintf(fout, "allow base_t base_t:%s *;\n", classlist[i]);
		else
			fprintf(fout, "allow base_t base_t:user%d *;\n", i);
	}
	fprintf(fout, "user user_u roles { base_r };\n");
	fprintf(fout, "\n");

	/* default sids */
	for (i=1; i < initial_sid_to_string_len; i++)
		fprintf(fout, "sid %s user_u:base_r:base_t\n", initial_sid_to_string[i]);
	fprintf(fout, "\n");


	fprintf(fout, "fs_use_xattr ext2 user_u:base_r:base_t;\n");
	fprintf(fout, "fs_use_xattr ext3 user_u:base_r:base_t;\n");
	fprintf(fout, "fs_use_xattr jfs user_u:base_r:base_t;\n");
	fprintf(fout, "fs_use_xattr xfs user_u:base_r:base_t;\n");
	fprintf(fout, "fs_use_xattr reiserfs user_u:base_r:base_t;\n");

	fprintf(fout, "fs_use_task pipefs user_u:base_r:base_t;\n");
	fprintf(fout, "fs_use_task sockfs user_u:base_r:base_t;\n");

	fprintf(fout, "fs_use_trans devpts user_u:base_r:base_t;\n");
	fprintf(fout, "fs_use_trans tmpfs user_u:base_r:base_t;\n");
	fprintf(fout, "fs_use_trans shm user_u:base_r:base_t;\n");

	fprintf(fout, "genfscon proc / user_u:base_r:base_t\n");

	fclose(fout);

	fout = fopen(ctxout, "w");
	if (!fout) {
		printf("Wrote policy, but cannot open %s for writing\n", ctxout);
		usage(argv[0]);
	}
	fprintf(fout, "/ user_u:base_r:base_t\n");
	fprintf(fout, "/.* user_u:base_r:base_t\n");
	fclose(fout);

	return 0;
}
