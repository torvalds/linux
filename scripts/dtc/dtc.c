/*
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2005.
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *                                                                   USA
 */

#include "dtc.h"
#include "srcpos.h"

/*
 * Command line options
 */
int quiet;		/* Level of quietness */
int reservenum;		/* Number of memory reservation slots */
int minsize;		/* Minimum blob size */
int padsize;		/* Additional padding to blob */
int phandle_format = PHANDLE_BOTH;	/* Use linux,phandle or phandle properties */

static void fill_fullpaths(struct node *tree, const char *prefix)
{
	struct node *child;
	const char *unit;

	tree->fullpath = join_path(prefix, tree->name);

	unit = strchr(tree->name, '@');
	if (unit)
		tree->basenamelen = unit - tree->name;
	else
		tree->basenamelen = strlen(tree->name);

	for_each_child(tree, child)
		fill_fullpaths(child, tree->fullpath);
}

/* Usage related data. */
static const char usage_synopsis[] = "dtc [options] <input file>";
static const char usage_short_opts[] = "qI:O:o:V:d:R:S:p:fb:i:H:sW:E:hv";
static struct option const usage_long_opts[] = {
	{"quiet",            no_argument, NULL, 'q'},
	{"in-format",         a_argument, NULL, 'I'},
	{"out",               a_argument, NULL, 'o'},
	{"out-format",        a_argument, NULL, 'O'},
	{"out-version",       a_argument, NULL, 'V'},
	{"out-dependency",    a_argument, NULL, 'd'},
	{"reserve",           a_argument, NULL, 'R'},
	{"space",             a_argument, NULL, 'S'},
	{"pad",               a_argument, NULL, 'p'},
	{"boot-cpu",          a_argument, NULL, 'b'},
	{"force",            no_argument, NULL, 'f'},
	{"include",           a_argument, NULL, 'i'},
	{"sort",             no_argument, NULL, 's'},
	{"phandle",           a_argument, NULL, 'H'},
	{"warning",           a_argument, NULL, 'W'},
	{"error",             a_argument, NULL, 'E'},
	{"help",             no_argument, NULL, 'h'},
	{"version",          no_argument, NULL, 'v'},
	{NULL,               no_argument, NULL, 0x0},
};
static const char * const usage_opts_help[] = {
	"\n\tQuiet: -q suppress warnings, -qq errors, -qqq all",
	"\n\tInput formats are:\n"
	 "\t\tdts - device tree source text\n"
	 "\t\tdtb - device tree blob\n"
	 "\t\tfs  - /proc/device-tree style directory",
	"\n\tOutput file",
	"\n\tOutput formats are:\n"
	 "\t\tdts - device tree source text\n"
	 "\t\tdtb - device tree blob\n"
	 "\t\tasm - assembler source",
	"\n\tBlob version to produce, defaults to %d (for dtb and asm output)", //, DEFAULT_FDT_VERSION);
	"\n\tOutput dependency file",
	"\n\ttMake space for <number> reserve map entries (for dtb and asm output)",
	"\n\tMake the blob at least <bytes> long (extra space)",
	"\n\tAdd padding to the blob of <bytes> long (extra space)",
	"\n\tSet the physical boot cpu",
	"\n\tTry to produce output even if the input tree has errors",
	"\n\tAdd a path to search for include files",
	"\n\tSort nodes and properties before outputting (useful for comparing trees)",
	"\n\tValid phandle formats are:\n"
	 "\t\tlegacy - \"linux,phandle\" properties only\n"
	 "\t\tepapr  - \"phandle\" properties only\n"
	 "\t\tboth   - Both \"linux,phandle\" and \"phandle\" properties",
	"\n\tEnable/disable warnings (prefix with \"no-\")",
	"\n\tEnable/disable errors (prefix with \"no-\")",
	"\n\tPrint this help and exit",
	"\n\tPrint version and exit",
	NULL,
};

int main(int argc, char *argv[])
{
	struct boot_info *bi;
	const char *inform = "dts";
	const char *outform = "dts";
	const char *outname = "-";
	const char *depname = NULL;
	int force = 0, sort = 0;
	const char *arg;
	int opt;
	FILE *outf = NULL;
	int outversion = DEFAULT_FDT_VERSION;
	long long cmdline_boot_cpuid = -1;

	quiet      = 0;
	reservenum = 0;
	minsize    = 0;
	padsize    = 0;

	while ((opt = util_getopt_long()) != EOF) {
		switch (opt) {
		case 'I':
			inform = optarg;
			break;
		case 'O':
			outform = optarg;
			break;
		case 'o':
			outname = optarg;
			break;
		case 'V':
			outversion = strtol(optarg, NULL, 0);
			break;
		case 'd':
			depname = optarg;
			break;
		case 'R':
			reservenum = strtol(optarg, NULL, 0);
			break;
		case 'S':
			minsize = strtol(optarg, NULL, 0);
			break;
		case 'p':
			padsize = strtol(optarg, NULL, 0);
			break;
		case 'f':
			force = 1;
			break;
		case 'q':
			quiet++;
			break;
		case 'b':
			cmdline_boot_cpuid = strtoll(optarg, NULL, 0);
			break;
		case 'i':
			srcfile_add_search_path(optarg);
			break;
		case 'v':
			util_version();
		case 'H':
			if (streq(optarg, "legacy"))
				phandle_format = PHANDLE_LEGACY;
			else if (streq(optarg, "epapr"))
				phandle_format = PHANDLE_EPAPR;
			else if (streq(optarg, "both"))
				phandle_format = PHANDLE_BOTH;
			else
				die("Invalid argument \"%s\" to -H option\n",
				    optarg);
			break;

		case 's':
			sort = 1;
			break;

		case 'W':
			parse_checks_option(true, false, optarg);
			break;

		case 'E':
			parse_checks_option(false, true, optarg);
			break;

		case 'h':
			usage(NULL);
		default:
			usage("unknown option");
		}
	}

	if (argc > (optind+1))
		usage("missing files");
	else if (argc < (optind+1))
		arg = "-";
	else
		arg = argv[optind];

	/* minsize and padsize are mutually exclusive */
	if (minsize && padsize)
		die("Can't set both -p and -S\n");

	if (depname) {
		depfile = fopen(depname, "w");
		if (!depfile)
			die("Couldn't open dependency file %s: %s\n", depname,
			    strerror(errno));
		fprintf(depfile, "%s:", outname);
	}

	if (streq(inform, "dts"))
		bi = dt_from_source(arg);
	else if (streq(inform, "fs"))
		bi = dt_from_fs(arg);
	else if(streq(inform, "dtb"))
		bi = dt_from_blob(arg);
	else
		die("Unknown input format \"%s\"\n", inform);

	if (depfile) {
		fputc('\n', depfile);
		fclose(depfile);
	}

	if (cmdline_boot_cpuid != -1)
		bi->boot_cpuid_phys = cmdline_boot_cpuid;

	fill_fullpaths(bi->dt, "");
	process_checks(force, bi);

	if (sort)
		sort_tree(bi);

	if (streq(outname, "-")) {
		outf = stdout;
	} else {
		outf = fopen(outname, "w");
		if (! outf)
			die("Couldn't open output file %s: %s\n",
			    outname, strerror(errno));
	}

	if (streq(outform, "dts")) {
		dt_to_source(outf, bi);
	} else if (streq(outform, "dtb")) {
		dt_to_blob(outf, bi, outversion);
	} else if (streq(outform, "asm")) {
		dt_to_asm(outf, bi, outversion);
	} else if (streq(outform, "null")) {
		/* do nothing */
	} else {
		die("Unknown output format \"%s\"\n", outform);
	}

	exit(0);
}
