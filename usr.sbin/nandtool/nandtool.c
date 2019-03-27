/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2012 Semihalf.
 * All rights reserved.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <sysexits.h>
#include <libgeom.h>
#include "nandtool.h"
#include "usage.h"

int usage(struct cmd_param *);

static const struct {
	const char	*name;
	const char	*usage;
	int		(*handler)(struct cmd_param *);
} commands[] = {
	{ "help", nand_help_usage, usage },
	{ "read", nand_read_usage, nand_read },
	{ "write", nand_write_usage, nand_write },
	{ "erase", nand_erase_usage, nand_erase },
	{ "readoob", nand_read_oob_usage, nand_read_oob },
	{ "writeoob", nand_write_oob_usage, nand_write_oob },
	{ "info", nand_info_usage, nand_info },
	{ NULL, NULL, NULL },
};

static char *
_param_get_stringx(struct cmd_param *params, const char *name, int doexit)
{
	int i;

	for (i = 0; params[i].name[0] != '\0'; i++) {
		if (!strcmp(params[i].name, name))
			return params[i].value;
	}

	if (doexit) {
		perrorf("Missing parameter %s", name);
		exit(1);
	}
	return (NULL);
}

char *
param_get_string(struct cmd_param *params, const char *name)
{

	return (_param_get_stringx(params, name, 0));
}

static int
_param_get_intx(struct cmd_param *params, const char *name, int doexit)
{
	int ret;
	char *str = _param_get_stringx(params, name, doexit);

	if (!str)
		return (-1);

	errno = 0;
	ret = (int)strtol(str, (char **)NULL, 10);
	if (errno) {
		if (doexit) {
			perrorf("Invalid value for parameter %s", name);
			exit(1);
		}
		return (-1);
	}

	return (ret);
}

int
param_get_intx(struct cmd_param *params, const char *name)
{

	return (_param_get_intx(params, name, 1));
}

int
param_get_int(struct cmd_param *params, const char *name)
{

	return (_param_get_intx(params, name, 0));
}

int
param_get_boolean(struct cmd_param *params, const char *name)
{
	char *str = param_get_string(params, name);

	if (!str)
		return (0);

	if (!strcmp(str, "true") || !strcmp(str, "yes"))
		return (1);

	return (0);
}

int
param_has_value(struct cmd_param *params, const char *name)
{
	int i;

	for (i = 0; params[i].name[0] != '\0'; i++) {
		if (!strcmp(params[i].name, name))
			return (1);
	}

	return (0);
}

int
param_get_count(struct cmd_param *params)
{
	int i;

	for (i = 0; params[i].name[0] != '\0'; i++);

	return (i);
}

void
hexdumpoffset(uint8_t *buf, int length, int off)
{
	int i, j;
	for (i = 0; i < length; i += 16) {
		printf("%08x: ", off + i);

		for (j = 0; j < 16; j++)
			printf("%02x ", buf[i+j]);

		printf("| ");

		for (j = 0; j < 16; j++) {
			printf("%c", isalnum(buf[i+j])
			    ? buf[i+j]
			    : '.');
		}

		printf("\n");
	}
}

void
hexdump(uint8_t *buf, int length)
{

	hexdumpoffset(buf, length, 0);
}

void *
xmalloc(size_t len)
{
	void *ret = malloc(len);

	if (!ret) {
		fprintf(stderr, "Cannot allocate buffer of %zd bytes. "
		    "Exiting.\n", len);
		exit(EX_OSERR);
	}

	return (ret);
}

void
perrorf(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fprintf(stderr, ": %s\n", strerror(errno));
}

int
usage(struct cmd_param *params)
{
	int i;

	if (!params || !param_get_count(params)) {
		fprintf(stderr, "Usage: nandtool <command> [arguments...]\n");
		fprintf(stderr, "Arguments are in form 'name=value'.\n\n");
		fprintf(stderr, "Available commands:\n");

		for (i = 0; commands[i].name != NULL; i++)
			fprintf(stderr, "\t%s\n", commands[i].name);

		fprintf(stderr, "\n");
		fprintf(stderr, "For information about particular command, "
		    "type:\n");
		fprintf(stderr, "'nandtool help topic=<command>'\n");
	} else if (param_has_value(params, "topic")) {
		for (i = 0; commands[i].name != NULL; i++) {
			if (!strcmp(param_get_string(params, "topic"),
			    commands[i].name)) {
				fprintf(stderr, commands[i].usage, "nandtool");
				return (0);
			}
		}

		fprintf(stderr, "No such command\n");
		return (EX_SOFTWARE);
	} else {
		fprintf(stderr, "Wrong arguments given. Try: 'nandtool help'\n");
	}

	return (EX_USAGE);
}

int
main(int argc, const char *argv[])
{
	struct cmd_param *params;
	int i, ret, idx;

	if (argc < 2) {
		usage(NULL);
		return (0);
	}

	params = malloc(sizeof(struct cmd_param) * (argc - 1));

	for (i = 2, idx = 0; i < argc; i++, idx++) {
		if (sscanf(argv[i], "%63[^=]=%63s", params[idx].name,
		    params[idx].value) < 2) {
			fprintf(stderr, "Syntax error in argument %d. "
			    "Argument should be in form 'name=value'.\n", i);
			free(params);
			return (-1);
		}
	}

	params[idx].name[0] = '\0';
	params[idx].value[0] = '\0';

	for (i = 0; commands[i].name != NULL; i++) {
		if (!strcmp(commands[i].name, argv[1])) {
			ret = commands[i].handler(params);
			free(params);
			return (ret);
		}
	}

	free(params);
	fprintf(stderr, "Unknown command. Try '%s help'\n", argv[0]);

	return (-1);
}

