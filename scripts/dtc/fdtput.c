// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 */

#include <assert.h>
#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libfdt.h>

#include "util.h"

/* These are the operations we support */
enum oper_type {
	OPER_WRITE_PROP,		/* Write a property in a node */
	OPER_CREATE_NODE,		/* Create a new node */
};

struct display_info {
	enum oper_type oper;	/* operation to perform */
	int type;		/* data type (s/i/u/x or 0 for default) */
	int size;		/* data size (1/2/4) */
	int verbose;		/* verbose output */
	int auto_path;		/* automatically create all path components */
};


/**
 * Report an error with a particular node.
 *
 * @param name		Node name to report error on
 * @param namelen	Length of node name, or -1 to use entire string
 * @param err		Error number to report (-FDT_ERR_...)
 */
static void report_error(const char *name, int namelen, int err)
{
	if (namelen == -1)
		namelen = strlen(name);
	fprintf(stderr, "Error at '%1.*s': %s\n", namelen, name,
		fdt_strerror(err));
}

/**
 * Encode a series of arguments in a property value.
 *
 * @param disp		Display information / options
 * @param arg		List of arguments from command line
 * @param arg_count	Number of arguments (may be 0)
 * @param valuep	Returns buffer containing value
 * @param *value_len	Returns length of value encoded
 */
static int encode_value(struct display_info *disp, char **arg, int arg_count,
			char **valuep, int *value_len)
{
	char *value = NULL;	/* holding area for value */
	int value_size = 0;	/* size of holding area */
	char *ptr;		/* pointer to current value position */
	int len;		/* length of this cell/string/byte */
	int ival;
	int upto;	/* the number of bytes we have written to buf */
	char fmt[3];

	upto = 0;

	if (disp->verbose)
		fprintf(stderr, "Decoding value:\n");

	fmt[0] = '%';
	fmt[1] = disp->type ? disp->type : 'd';
	fmt[2] = '\0';
	for (; arg_count > 0; arg++, arg_count--, upto += len) {
		/* assume integer unless told otherwise */
		if (disp->type == 's')
			len = strlen(*arg) + 1;
		else
			len = disp->size == -1 ? 4 : disp->size;

		/* enlarge our value buffer by a suitable margin if needed */
		if (upto + len > value_size) {
			value_size = (upto + len) + 500;
			value = realloc(value, value_size);
			if (!value) {
				fprintf(stderr, "Out of mmory: cannot alloc "
					"%d bytes\n", value_size);
				return -1;
			}
		}

		ptr = value + upto;
		if (disp->type == 's') {
			memcpy(ptr, *arg, len);
			if (disp->verbose)
				fprintf(stderr, "\tstring: '%s'\n", ptr);
		} else {
			int *iptr = (int *)ptr;
			sscanf(*arg, fmt, &ival);
			if (len == 4)
				*iptr = cpu_to_fdt32(ival);
			else
				*ptr = (uint8_t)ival;
			if (disp->verbose) {
				fprintf(stderr, "\t%s: %d\n",
					disp->size == 1 ? "byte" :
					disp->size == 2 ? "short" : "int",
					ival);
			}
		}
	}
	*value_len = upto;
	*valuep = value;
	if (disp->verbose)
		fprintf(stderr, "Value size %d\n", upto);
	return 0;
}

static int store_key_value(void *blob, const char *node_name,
		const char *property, const char *buf, int len)
{
	int node;
	int err;

	node = fdt_path_offset(blob, node_name);
	if (node < 0) {
		report_error(node_name, -1, node);
		return -1;
	}

	err = fdt_setprop(blob, node, property, buf, len);
	if (err) {
		report_error(property, -1, err);
		return -1;
	}
	return 0;
}

/**
 * Create paths as needed for all components of a path
 *
 * Any components of the path that do not exist are created. Errors are
 * reported.
 *
 * @param blob		FDT blob to write into
 * @param in_path	Path to process
 * @return 0 if ok, -1 on error
 */
static int create_paths(void *blob, const char *in_path)
{
	const char *path = in_path;
	const char *sep;
	int node, offset = 0;

	/* skip leading '/' */
	while (*path == '/')
		path++;

	for (sep = path; *sep; path = sep + 1, offset = node) {
		/* equivalent to strchrnul(), but it requires _GNU_SOURCE */
		sep = strchr(path, '/');
		if (!sep)
			sep = path + strlen(path);

		node = fdt_subnode_offset_namelen(blob, offset, path,
				sep - path);
		if (node == -FDT_ERR_NOTFOUND) {
			node = fdt_add_subnode_namelen(blob, offset, path,
						       sep - path);
		}
		if (node < 0) {
			report_error(path, sep - path, node);
			return -1;
		}
	}

	return 0;
}

/**
 * Create a new node in the fdt.
 *
 * This will overwrite the node_name string. Any error is reported.
 *
 * TODO: Perhaps create fdt_path_offset_namelen() so we don't need to do this.
 *
 * @param blob		FDT blob to write into
 * @param node_name	Name of node to create
 * @return new node offset if found, or -1 on failure
 */
static int create_node(void *blob, const char *node_name)
{
	int node = 0;
	char *p;

	p = strrchr(node_name, '/');
	if (!p) {
		report_error(node_name, -1, -FDT_ERR_BADPATH);
		return -1;
	}
	*p = '\0';

	if (p > node_name) {
		node = fdt_path_offset(blob, node_name);
		if (node < 0) {
			report_error(node_name, -1, node);
			return -1;
		}
	}

	node = fdt_add_subnode(blob, node, p + 1);
	if (node < 0) {
		report_error(p + 1, -1, node);
		return -1;
	}

	return 0;
}

static int do_fdtput(struct display_info *disp, const char *filename,
		    char **arg, int arg_count)
{
	char *value;
	char *blob;
	int len, ret = 0;

	blob = utilfdt_read(filename);
	if (!blob)
		return -1;

	switch (disp->oper) {
	case OPER_WRITE_PROP:
		/*
		 * Convert the arguments into a single binary value, then
		 * store them into the property.
		 */
		assert(arg_count >= 2);
		if (disp->auto_path && create_paths(blob, *arg))
			return -1;
		if (encode_value(disp, arg + 2, arg_count - 2, &value, &len) ||
			store_key_value(blob, *arg, arg[1], value, len))
			ret = -1;
		break;
	case OPER_CREATE_NODE:
		for (; ret >= 0 && arg_count--; arg++) {
			if (disp->auto_path)
				ret = create_paths(blob, *arg);
			else
				ret = create_node(blob, *arg);
		}
		break;
	}
	if (ret >= 0)
		ret = utilfdt_write(filename, blob);

	free(blob);
	return ret;
}

static const char *usage_msg =
	"fdtput - write a property value to a device tree\n"
	"\n"
	"The command line arguments are joined together into a single value.\n"
	"\n"
	"Usage:\n"
	"	fdtput <options> <dt file> <node> <property> [<value>...]\n"
	"	fdtput -c <options> <dt file> [<node>...]\n"
	"Options:\n"
	"\t-c\t\tCreate nodes if they don't already exist\n"
	"\t-p\t\tAutomatically create nodes as needed for the node path\n"
	"\t-t <type>\tType of data\n"
	"\t-v\t\tVerbose: display each value decoded from command line\n"
	"\t-h\t\tPrint this help\n\n"
	USAGE_TYPE_MSG;

static void usage(const char *msg)
{
	if (msg)
		fprintf(stderr, "Error: %s\n\n", msg);

	fprintf(stderr, "%s", usage_msg);
	exit(2);
}

int main(int argc, char *argv[])
{
	struct display_info disp;
	char *filename = NULL;

	memset(&disp, '\0', sizeof(disp));
	disp.size = -1;
	disp.oper = OPER_WRITE_PROP;
	for (;;) {
		int c = getopt(argc, argv, "chpt:v");
		if (c == -1)
			break;

		/*
		 * TODO: add options to:
		 * - delete property
		 * - delete node (optionally recursively)
		 * - rename node
		 * - pack fdt before writing
		 * - set amount of free space when writing
		 * - expand fdt if value doesn't fit
		 */
		switch (c) {
		case 'c':
			disp.oper = OPER_CREATE_NODE;
			break;
		case 'h':
		case '?':
			usage(NULL);
		case 'p':
			disp.auto_path = 1;
			break;
		case 't':
			if (utilfdt_decode_type(optarg, &disp.type,
					&disp.size))
				usage("Invalid type string");
			break;

		case 'v':
			disp.verbose = 1;
			break;
		}
	}

	if (optind < argc)
		filename = argv[optind++];
	if (!filename)
		usage("Missing filename");

	argv += optind;
	argc -= optind;

	if (disp.oper == OPER_WRITE_PROP) {
		if (argc < 1)
			usage("Missing node");
		if (argc < 2)
			usage("Missing property");
	}

	if (do_fdtput(&disp, filename, argv, argc))
		return 1;
	return 0;
}
