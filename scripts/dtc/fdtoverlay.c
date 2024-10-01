// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2017 Konsulko Group Inc. All rights reserved.
 *
 * Author:
 *	 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 */

#include <assert.h>
#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <libfdt.h>

#include "util.h"

#define BUF_INCREMENT	65536

/* Usage related data. */
static const char usage_synopsis[] =
	"apply a number of overlays to a base blob\n"
	"	fdtoverlay <options> [<overlay.dtbo> [<overlay.dtbo>]]\n"
	"\n"
	USAGE_TYPE_MSG;
static const char usage_short_opts[] = "i:o:v" USAGE_COMMON_SHORT_OPTS;
static struct option const usage_long_opts[] = {
	{"input",            required_argument, NULL, 'i'},
	{"output",	     required_argument, NULL, 'o'},
	{"verbose",	           no_argument, NULL, 'v'},
	USAGE_COMMON_LONG_OPTS,
};
static const char * const usage_opts_help[] = {
	"Input base DT blob",
	"Output DT blob",
	"Verbose messages",
	USAGE_COMMON_OPTS_HELP
};

int verbose = 0;

static void *apply_one(char *base, const char *overlay, size_t *buf_len,
		       const char *name)
{
	char *tmp = NULL;
	char *tmpo;
	int ret;

	/*
	 * We take a copies first, because a a failed apply can trash
	 * both the base blob and the overlay
	 */
	tmpo = xmalloc(fdt_totalsize(overlay));

	do {
		tmp = xrealloc(tmp, *buf_len);
		ret = fdt_open_into(base, tmp, *buf_len);
		if (ret) {
			fprintf(stderr,
				"\nFailed to make temporary copy: %s\n",
				fdt_strerror(ret));
			goto fail;
		}

		memcpy(tmpo, overlay, fdt_totalsize(overlay));

		ret = fdt_overlay_apply(tmp, tmpo);
		if (ret == -FDT_ERR_NOSPACE) {
			*buf_len += BUF_INCREMENT;
		}
	} while (ret == -FDT_ERR_NOSPACE);

	if (ret) {
		fprintf(stderr, "\nFailed to apply '%s': %s\n",
			name, fdt_strerror(ret));
		goto fail;
	}

	free(base);
	free(tmpo);
	return tmp;

fail:
	free(tmpo);
	if (tmp)
		free(tmp);

	return NULL;
}
static int do_fdtoverlay(const char *input_filename,
			 const char *output_filename,
			 int argc, char *argv[])
{
	char *blob = NULL;
	char **ovblob = NULL;
	size_t buf_len;
	int i, ret = -1;

	blob = utilfdt_read(input_filename, &buf_len);
	if (!blob) {
		fprintf(stderr, "\nFailed to read '%s'\n", input_filename);
		goto out_err;
	}
	if (fdt_totalsize(blob) > buf_len) {
		fprintf(stderr,
 "\nBase blob is incomplete (%lu / %" PRIu32 " bytes read)\n",
			(unsigned long)buf_len, fdt_totalsize(blob));
		goto out_err;
	}

	/* allocate blob pointer array */
	ovblob = xmalloc(sizeof(*ovblob) * argc);
	memset(ovblob, 0, sizeof(*ovblob) * argc);

	/* read and keep track of the overlay blobs */
	for (i = 0; i < argc; i++) {
		size_t ov_len;
		ovblob[i] = utilfdt_read(argv[i], &ov_len);
		if (!ovblob[i]) {
			fprintf(stderr, "\nFailed to read '%s'\n", argv[i]);
			goto out_err;
		}
		if (fdt_totalsize(ovblob[i]) > ov_len) {
			fprintf(stderr,
"\nOverlay '%s' is incomplete (%lu / %" PRIu32 " bytes read)\n",
				argv[i], (unsigned long)ov_len,
				fdt_totalsize(ovblob[i]));
			goto out_err;
		}
	}

	buf_len = fdt_totalsize(blob);

	/* apply the overlays in sequence */
	for (i = 0; i < argc; i++) {
		blob = apply_one(blob, ovblob[i], &buf_len, argv[i]);
		if (!blob)
			goto out_err;
	}

	fdt_pack(blob);
	ret = utilfdt_write(output_filename, blob);
	if (ret)
		fprintf(stderr, "\nFailed to write '%s'\n",
			output_filename);

out_err:
	if (ovblob) {
		for (i = 0; i < argc; i++) {
			if (ovblob[i])
				free(ovblob[i]);
		}
		free(ovblob);
	}
	free(blob);

	return ret;
}

int main(int argc, char *argv[])
{
	int opt, i;
	char *input_filename = NULL;
	char *output_filename = NULL;

	while ((opt = util_getopt_long()) != EOF) {
		switch (opt) {
		case_USAGE_COMMON_FLAGS

		case 'i':
			input_filename = optarg;
			break;
		case 'o':
			output_filename = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		}
	}

	if (!input_filename)
		usage("missing input file");

	if (!output_filename)
		usage("missing output file");

	argv += optind;
	argc -= optind;

	if (argc <= 0)
		usage("missing overlay file(s)");

	if (verbose) {
		printf("input  = %s\n", input_filename);
		printf("output = %s\n", output_filename);
		for (i = 0; i < argc; i++)
			printf("overlay[%d] = %s\n", i, argv[i]);
	}

	if (do_fdtoverlay(input_filename, output_filename, argc, argv))
		return 1;

	return 0;
}
