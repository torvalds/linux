/*-
 * Copyright (c) 2013 Stacey D. Son
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/imgact_binmisc.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/sysctl.h>

enum cmd {
	CMD_ADD = 0,
	CMD_REMOVE,
	CMD_DISABLE,
	CMD_ENABLE,
	CMD_LOOKUP,
	CMD_LIST,
};

extern char *__progname;

typedef int (*cmd_func_t)(int argc, char *argv[], ximgact_binmisc_entry_t *xbe);

int add_cmd(int argc, char *argv[], ximgact_binmisc_entry_t *xbe);
int name_cmd(int argc, char *argv[], ximgact_binmisc_entry_t *xbe);
int noname_cmd(int argc, char *argv[], ximgact_binmisc_entry_t *xbe);

static const struct {
	const int token;
	const char *name;
	cmd_func_t func;
	const char *desc;
	const char *args;
} cmds[] = {
	{
		CMD_ADD,
		"add",
		add_cmd,
		"Add a new binary image activator (requires 'root' privilege)",
		"<name> --interpreter <path_and_arguments> \\\n"
		"\t\t--magic <magic_bytes> [--mask <mask_bytes>] \\\n"
		"\t\t--size <magic_size> [--offset <magic_offset>] \\\n"
		"\t\t[--set-enabled]"
	},
	{
		CMD_REMOVE,
		"remove",
		name_cmd,
		"Remove a binary image activator (requires 'root' privilege)",
		"<name>"
	},
	{
		CMD_DISABLE,
		"disable",
		name_cmd,
		"Disable a binary image activator (requires 'root' privilege)",
		"<name>"
	},
	{
		CMD_ENABLE,
		"enable",
		name_cmd,
		"Enable a binary image activator (requires 'root' privilege)",
		"<name>"
	},
	{
		CMD_LOOKUP,
		"lookup",
		name_cmd,
		"Lookup a binary image activator",
		"<name>"
	},
	{
		CMD_LIST,
		"list",
		noname_cmd,
		"List all the binary image activators",
		""
	},
};

static const struct option
add_opts[] = {
	{ "set-enabled",	no_argument,		NULL,	'e' },
	{ "interpreter",	required_argument,	NULL,	'i' },
	{ "mask",		required_argument,	NULL,	'M' },
	{ "magic",		required_argument,	NULL,	'm' },
	{ "offset",		required_argument,	NULL,	'o' },
	{ "size",		required_argument,	NULL,	's' },
	{ NULL,			0,			NULL,	0   }
};

static char const *cmd_sysctl_name[] = {
	IBE_SYSCTL_NAME_ADD,
	IBE_SYSCTL_NAME_REMOVE,
	IBE_SYSCTL_NAME_DISABLE,
	IBE_SYSCTL_NAME_ENABLE,
	IBE_SYSCTL_NAME_LOOKUP,
	IBE_SYSCTL_NAME_LIST
};

static void
usage(const char *format, ...)
{
	va_list args;
	size_t i;
	int error = 0;

	va_start(args, format);
	if (format) {
		vfprintf(stderr, format, args);
		error = -1;
	}
	va_end(args);
	fprintf(stderr, "\n");
	fprintf(stderr, "usage: %s command [args...]\n\n", __progname);

	for(i = 0; i < ( sizeof (cmds) / sizeof (cmds[0])); i++) {
		fprintf(stderr, "%s:\n", cmds[i].desc);
		fprintf(stderr, "\t%s %s %s\n\n", __progname, cmds[i].name,
		    cmds[i].args);
	}

	exit (error);
}

static void
fatal(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	if (format)
		vfprintf(stderr, format, args);
	fprintf(stderr, "\n");

	exit(-1);
}

static void
getoptstr(char *str, size_t size, const char *argname)
{
	if (strlen(optarg) > size)
		usage("'%s' too large", argname);
	strlcpy(str, optarg, size);
}

static void
printxbe(ximgact_binmisc_entry_t *xbe)
{
	uint32_t i, flags = xbe->xbe_flags;

	if (xbe->xbe_version != IBE_VERSION) {
		fprintf(stderr, "Error: XBE version mismatch\n");
		return;
	}

	printf("name: %s\n", xbe->xbe_name);
	printf("interpreter: %s\n", xbe->xbe_interpreter);
	printf("flags: %s%s\n", (flags & IBF_ENABLED) ? "ENABLED " : "",
	    (flags & IBF_USE_MASK) ? "USE_MASK " : "");
	printf("magic size: %u\n", xbe->xbe_msize);
	printf("magic offset: %u\n", xbe->xbe_moffset);

	printf("magic: ");
	for(i = 0; i < xbe->xbe_msize;  i++) {
		if (i && !(i % 12))
			printf("\n       ");
		else
			if (i && !(i % 4))
				printf(" ");
		printf("0x%02x ", xbe->xbe_magic[i]);
	}
	printf("\n");

	if (flags & IBF_USE_MASK) {
		printf("mask:  ");
		for(i = 0; i < xbe->xbe_msize;  i++) {
			if (i && !(i % 12))
				printf("\n       ");
			else
				if (i && !(i % 4))
					printf(" ");
			printf("0x%02x ", xbe->xbe_mask[i]);
		}
		printf("\n");
	}

	printf("\n");
}

static int
demux_cmd(__unused int argc, char *const argv[])
{
	size_t i;

	optind = 1;
	optreset = 1;

	for(i = 0; i < ( sizeof (cmds) / sizeof (cmds[0])); i++) {
		if (!strcasecmp(cmds[i].name, argv[0])) {
			return (i);
		}
	}

	/* Unknown command */
	return (-1);
}

static int
strlit2bin_cpy(uint8_t *d, char *s, size_t size)
{
	int c;
	size_t cnt = 0;

	while((c = *s++) != '\0') {
		if (c == '\\') {
			/* Do '\' escapes. */
			switch (*s) {
			case '\\':
				*d++ = '\\';
				break;

			case 'x':
				s++;
				c = toupper(*s++);
				*d = (c - (isdigit(c) ? '0' : ('A' - 10))) << 4;
				c = toupper(*s++);
				*d++ |= c - (isdigit(c) ? '0' : ('A' - 10));
				break;

			default:
				return (-1);
			}
		} else
			*d++ = c;

		if (++cnt > size)
			return (-1);
	}

	return (cnt);
}

int
add_cmd(__unused int argc, char *argv[], ximgact_binmisc_entry_t *xbe)
{
	int ch;
	char *magic = NULL, *mask = NULL;
	int sz;

	if (strlen(argv[0]) > IBE_NAME_MAX)
		usage("'%s' string length longer than IBE_NAME_MAX (%d)",
		    IBE_NAME_MAX);
	strlcpy(&xbe->xbe_name[0], argv[0], IBE_NAME_MAX);

	while ((ch = getopt_long(argc, argv, "ei:m:M:o:s:", add_opts, NULL))
	    != -1) {

		switch(ch) {
		case 'i':
			getoptstr(xbe->xbe_interpreter, IBE_INTERP_LEN_MAX,
			    "interpreter");
			break;

		case 'm':
			free(magic);
			magic = strdup(optarg);
			break;

		case 'M':
			free(mask);
			mask = strdup(optarg);
			xbe->xbe_flags |= IBF_USE_MASK;
			break;

		case 'e':
			xbe->xbe_flags |= IBF_ENABLED;
			break;

		case 'o':
			xbe->xbe_moffset = atol(optarg);
			break;

		case 's':
			xbe->xbe_msize = atol(optarg);
			if (xbe->xbe_msize == 0 ||
			    xbe->xbe_msize > IBE_MAGIC_MAX)
				usage("Error: Not valid '--size' value. "
				    "(Must be > 0 and < %u.)\n",
				    xbe->xbe_msize);
			break;

		default:
			usage("Unknown argument: '%c'", ch);
		}
	}

	if (xbe->xbe_msize == 0) {
		if (NULL != magic)
			free(magic);
		if (NULL != mask)
			free(mask);
		usage("Error: Missing '--size' argument");
	}

	if (NULL != magic) {
		if (xbe->xbe_msize == 0) {
			if (magic)
				free(magic);
			if (mask)
				free(mask);
			usage("Error: Missing magic size argument");
		}
		sz = strlit2bin_cpy(xbe->xbe_magic, magic, IBE_MAGIC_MAX);
		free(magic);
		if (sz == -1 || (uint32_t)sz != xbe->xbe_msize) {
			if (mask)
				free(mask);
			usage("Error: invalid magic argument");
		}
		if (mask) {
			sz = strlit2bin_cpy(xbe->xbe_mask, mask, IBE_MAGIC_MAX);
			free(mask);
			if (sz == -1 || (uint32_t)sz != xbe->xbe_msize)
				usage("Error: invalid mask argument");
		}
	} else {
		if (mask)
			free(mask);
		usage("Error: Missing magic argument");
	}

	if (!strnlen(xbe->xbe_interpreter, IBE_INTERP_LEN_MAX)) {
		usage("Error: Missing 'interpreter' argument");
	}

	return (0);
}

int
name_cmd(int argc, char *argv[], ximgact_binmisc_entry_t *xbe)
{
	if (argc == 0)
		usage("Required argument missing\n");
	if (strlen(argv[0]) > IBE_NAME_MAX)
		usage("'%s' string length longer than IBE_NAME_MAX (%d)",
		    IBE_NAME_MAX);
	strlcpy(&xbe->xbe_name[0], argv[0], IBE_NAME_MAX);

	return (0);
}

int
noname_cmd(__unused int argc, __unused char *argv[],
    __unused ximgact_binmisc_entry_t *xbe)
{

	return (0);
}

int
main(int argc, char **argv)
{
	int error = 0, cmd = -1;
	ximgact_binmisc_entry_t xbe_in, *xbe_inp = NULL;
	ximgact_binmisc_entry_t xbe_out, *xbe_outp = NULL;
	size_t xbe_in_sz = 0;
	size_t xbe_out_sz = 0, *xbe_out_szp = NULL;
	uint32_t i;

	if (modfind(KMOD_NAME) == -1) {
		if (kldload(KMOD_NAME) == -1)
			fatal("Can't load %s kernel module: %s",
			    KMOD_NAME, strerror(errno));
	}

	bzero(&xbe_in, sizeof(xbe_in));
	bzero(&xbe_out, sizeof(xbe_out));
	xbe_in.xbe_version = IBE_VERSION;

	if (argc < 2)
		usage("Error: requires at least one argument");

	argc--, argv++;
	cmd = demux_cmd(argc, argv);
	if (cmd < 0)
		usage("Error: Unknown command \"%s\"", argv[0]);
	argc--, argv++;

	error = (*cmds[cmd].func)(argc, argv, &xbe_in);
	if (error)
		usage("Can't parse command-line for '%s' command",
		    cmds[cmd].name);

	if (cmd != CMD_LIST) {
		xbe_inp = &xbe_in;
		xbe_in_sz = sizeof(xbe_in);
	} else
		xbe_out_szp = &xbe_out_sz;
	if (cmd == CMD_LOOKUP) {
		xbe_out_sz = sizeof(xbe_out);
		xbe_outp = &xbe_out;
		xbe_out_szp = &xbe_out_sz;
	}

	error = sysctlbyname(cmd_sysctl_name[cmd], xbe_outp, xbe_out_szp,
	    xbe_inp, xbe_in_sz);

	if (error)
		switch(errno) {
		case EINVAL:
			usage("Invalid interpreter name or --interpreter, "
			    "--magic, --mask, or --size argument value");
			break;

		case EEXIST:
			usage("'%s' is not unique in activator list",
			    xbe_in.xbe_name);
			break;

		case ENOENT:
			usage("'%s' is not found in activator list",
			    xbe_in.xbe_name);
			break;

		case ENOSPC:
			fatal("Fatal: no more room in the activator list "
			    "(limited to %d enties)", IBE_MAX_ENTRIES);
			break;

		case EPERM:
			usage("Insufficient privileges for '%s' command",
			    cmds[cmd].name);
			break;

		default:
			fatal("Fatal: sysctlbyname() returned: %s",
			    strerror(errno));
			break;
		}


	if (cmd == CMD_LOOKUP)
		printxbe(xbe_outp);

	if (cmd == CMD_LIST && xbe_out_sz > 0) {
		xbe_outp = malloc(xbe_out_sz);
		if (!xbe_outp)
			fatal("Fatal: out of memory");
		while(1) {
			size_t osize = xbe_out_sz;
			error = sysctlbyname(cmd_sysctl_name[cmd], xbe_outp,
			    &xbe_out_sz, NULL, 0);

			if (error == -1 && errno == ENOMEM &&
			    xbe_out_sz == osize) {
				/*
				 * Buffer too small. Increase it by one
				 * entry.
				 */
				xbe_out_sz += sizeof(xbe_out);
				xbe_outp = realloc(xbe_outp, xbe_out_sz);
				if (!xbe_outp)
					fatal("Fatal: out of memory");
			} else
				break;
		}
		if (error) {
			free(xbe_outp);
			fatal("Fatal: %s", strerror(errno));
		}
		for(i = 0; i < (xbe_out_sz / sizeof(xbe_out)); i++)
			printxbe(&xbe_outp[i]);
	}

	return (error);
}
