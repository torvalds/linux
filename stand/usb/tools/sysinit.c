/* $FreeBSD$ */
/*-
 * Copyright (c) 2013 Hans Petter Selasky. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This utility sorts sysinit structure entries in binary format and
 * prints out the result in C-format.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <err.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sysexits.h>
#include "sysinit.h"

static int opt_R;
static const char *input_f;
static const char *output_f;
static const char *struct_name;
static const char *keyword;
static struct sysinit_data **start;
static struct sysinit_data **stop;

static int input_file = -1;
static int output_file = -1;

static uint8_t *input_ptr;
static uint32_t input_len;

static uint32_t endian32;

static char scratch_buf[4096];

static int success;

static void do_sysinit(void);

/* the following function converts the numbers into host endian format */

static uint32_t
read32(uint32_t val)
{
	uint32_t temp;
	uint32_t endian;

	endian = endian32;
	temp = 0;

	while (val) {
		temp |= (val & 0xF) << ((endian & 0xF) * 4);
		endian >>= 4;
		val >>= 4;
	}
	return (temp);
}

static void
do_write(int fd, const char *buf)
{
	int len = strlen(buf);

	if (write(fd, buf, len) != len)
		err(EX_SOFTWARE, "Could not write to output file");
}

static void *
do_malloc(int size)
{
	void *ptr;

	ptr = malloc(size);
	if (ptr == NULL)
		errx(EX_SOFTWARE, "Could not allocate memory");
	return (ptr);
}

static void
usage(void)
{
	errx(EX_USAGE, "sysinit -i sysinit.bin -o sysinit_data.c \\\n"
	    "\t" "-k sysinit -s sysinit_data [ -R (reverse)]");
}

static void
cleanup(void)
{
	if (output_file >= 0)
		close(output_file);
	if (input_file >= 0)
		close(input_file);
	if (success == 0) {
		if (output_f)
			unlink(output_f);
	}
}

static int
compare(const void *_pa, const void *_pb)
{
	const struct sysinit_data * const *pa = _pa;
	const struct sysinit_data * const *pb = _pb;

	if ((*pa)->dw_msb_value > (*pb)->dw_msb_value)
		return (1);

	if ((*pa)->dw_msb_value < (*pb)->dw_msb_value)
		return (-1);

	if ((*pa)->dw_lsb_value > (*pb)->dw_lsb_value)
		return (1);

	if ((*pa)->dw_lsb_value < (*pb)->dw_lsb_value)
		return (-1);

	return (0);	/* equal */
}

static int
compare_R(const void *_pa, const void *_pb)
{
	const struct sysinit_data * const *pa = _pa;
	const struct sysinit_data * const *pb = _pb;

	if ((*pa)->dw_msb_value > (*pb)->dw_msb_value)
		return (-1);

	if ((*pa)->dw_msb_value < (*pb)->dw_msb_value)
		return (1);

	if ((*pa)->dw_lsb_value > (*pb)->dw_lsb_value)
		return (-1);

	if ((*pa)->dw_lsb_value < (*pb)->dw_lsb_value)
		return (1);

	return (0);	/* equal */
}

int
main(int argc, char **argv)
{
	struct sysinit_data **sipp;
	int c;
	int entries;
	off_t off;

	while ((c = getopt(argc, argv, "k:s:i:o:Rh")) != -1) {
		switch (c) {
		case 'i':
			input_f = optarg;
			break;
		case 'o':
			output_f = optarg;
			break;
		case 'R':
			opt_R = 1;
			break;
		case 'k':
			keyword = optarg;
			break;
		case 's':
			struct_name = optarg;
			break;
		default:
			usage();
		}
	}

	if (input_f == NULL || output_f == NULL ||
	    struct_name == NULL || keyword == NULL)
		usage();

	atexit(&cleanup);

	cleanup();

	input_file = open(input_f, O_RDONLY);
	if (input_file < 0)
		err(EX_SOFTWARE, "Could not open input file: %s", input_f);

	output_file = open(output_f, O_TRUNC | O_CREAT | O_RDWR, 0600);
	if (output_file < 0)
		err(EX_SOFTWARE, "Could not open output file: %s", output_f);

	off = lseek(input_file, 0, SEEK_END);

	input_ptr = do_malloc(off);
	input_len = off;

	if (input_len % (uint32_t)sizeof(struct sysinit_data)) {
		errx(EX_SOFTWARE, "Input file size is not divisible by %u",
		    (unsigned int)sizeof(struct sysinit_data));
	}
	off = lseek(input_file, 0, SEEK_SET);
	if (off < 0)
		err(EX_SOFTWARE, "Could not seek to start of input file");

	if (read(input_file, input_ptr, input_len) != input_len)
		err(EX_SOFTWARE, "Could not read input file");

	entries = input_len / (uint32_t)sizeof(struct sysinit_data);

	start = do_malloc(sizeof(void *) * entries);
	stop = start + entries;

	for (c = 0; c != entries; c++)
		start[c] = &((struct sysinit_data *)input_ptr)[c];

	if (start != stop)
		endian32 = (*start)->dw_endian32;

	/* switch all fields to host endian order */
	for (sipp = start; sipp < stop; sipp++) {
		(*sipp)->dw_lsb_value = read32((*sipp)->dw_lsb_value);
		(*sipp)->dw_msb_value = read32((*sipp)->dw_msb_value);
		(*sipp)->dw_file_line = read32((*sipp)->dw_file_line);
	}

	if (opt_R == 0) {
		/* sort entries, rising numerical order */
		qsort(start, entries, sizeof(void *), &compare);
	} else {
		/* sort entries, falling numerical order */
		qsort(start, entries, sizeof(void *), &compare_R);
	}

	/* safe all strings */
	for (sipp = start; sipp < stop; sipp++) {
		(*sipp)->b_keyword_name[sizeof((*sipp)->b_keyword_name) - 1] = 0;
		(*sipp)->b_global_type[sizeof((*sipp)->b_global_type) - 1] = 0;
		(*sipp)->b_global_name[sizeof((*sipp)->b_global_name) - 1] = 0;
		(*sipp)->b_file_name[sizeof((*sipp)->b_file_name) - 1] = 0;
		(*sipp)->b_debug_info[sizeof((*sipp)->b_debug_info) - 1] = 0;
	}

	if (strcmp(keyword, "sysinit") == 0)
		do_sysinit();
	else if (strcmp(keyword, "sysuninit") == 0)
		do_sysinit();
	else
		errx(EX_USAGE, "Unknown keyword '%s'", keyword);

	success = 1;

	return (0);
}

static void
do_sysinit(void)
{
	struct sysinit_data **sipp;
	int c;

	snprintf(scratch_buf, sizeof(scratch_buf),
	    "/*\n"
	    " * This file was automatically generated.\n"
	    " * Please do not edit.\n"
	    " */\n\n");

	/* write out externals */
	for (c = 0, sipp = start; sipp < stop; c++, sipp++) {
		if (strcmp((const char *)(*sipp)->b_keyword_name, keyword))
			continue;
		if ((*sipp)->dw_msb_value == 0)
			continue;

		snprintf(scratch_buf, sizeof(scratch_buf),
		    "/* #%04u: %s entry at %s:%u */\n",
		    c, (*sipp)->b_debug_info, (*sipp)->b_file_name,
		    (unsigned int)(*sipp)->dw_file_line);

		do_write(output_file, scratch_buf);

		snprintf(scratch_buf, sizeof(scratch_buf),
		    "extern %s %s;\n\n", (*sipp)->b_global_type,
		    (*sipp)->b_global_name);

		do_write(output_file, scratch_buf);
	}

	snprintf(scratch_buf, sizeof(scratch_buf),
	    "const void *%s[] = {\n", struct_name);

	do_write(output_file, scratch_buf);

	/* write out actual table */
	for (c = 0, sipp = start; sipp < stop; c++, sipp++) {
		if (strcmp((const char *)(*sipp)->b_keyword_name, keyword))
			continue;
		if ((*sipp)->dw_msb_value == 0)
			continue;

		snprintf(scratch_buf, sizeof(scratch_buf),
		    "\t&%s, /* #%04u */\n",
		    (*sipp)->b_global_name, (unsigned int)c);

		do_write(output_file, scratch_buf);
	}

	snprintf(scratch_buf, sizeof(scratch_buf),
	    "\t(const void *)0\n"
	    "};\n");

	do_write(output_file, scratch_buf);
}
