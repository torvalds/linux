/*-
 * Copyright (c) 2016 Netflix, Inc.
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

#include <ctype.h>
#include <efivar.h>
#include <efivar-dp.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "efiutil.h"
#include "efichar.h"

/* options descriptor */
static struct option longopts[] = {
	{ "append",		no_argument,		NULL,	'a' },
	{ "ascii",		no_argument,		NULL,	'A' },
	{ "attributes",		required_argument,	NULL,	't' },
	{ "binary",		no_argument,		NULL,	'b' },
	{ "delete",		no_argument,		NULL,   'D' },
	{ "device",		no_argument,		NULL,   'd' },
	{ "device-path",	no_argument,		NULL,   'd' },
	{ "fromfile",		required_argument,	NULL,	'f' },
	{ "guid",		no_argument,		NULL,	'g' },
	{ "hex",		no_argument,		NULL,	'H' },
	{ "list-guids",		no_argument,		NULL,	'L' },
	{ "list",		no_argument,		NULL,	'l' },
	{ "load-option",	no_argument,		NULL,	'O' },
	{ "name",		required_argument,	NULL,	'n' },
	{ "no-name",		no_argument,		NULL,	'N' },
	{ "print",		no_argument,		NULL,	'p' },
	{ "print-decimal",	no_argument,		NULL,	'd' },
	{ "raw-guid",		no_argument,		NULL,   'R' },
	{ "utf8",		no_argument,		NULL,	'u' },
	{ "write",		no_argument,		NULL,	'w' },
	{ NULL,			0,			NULL,	0 }
};


static int aflag, Aflag, bflag, dflag, Dflag, gflag, Hflag, Nflag,
	lflag, Lflag, Rflag, wflag, pflag, uflag, load_opt_flag;
static char *varname;
static char *fromfile;
static u_long attrib = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS;

static void
usage(void)
{

	errx(1, "efivar [-abdDHlLNpRtuw] [-n name] [-f file] [--append] [--ascii]\n"
	    "\t[--attributes] [--binary] [--delete] [--fromfile file] [--hex]\n"
	    "\t[--list-guids] [--list] [--load-option] [--name name] [--no-name]\n"
	    "\t[--print] [--print-decimal] [--raw-guid] [--utf8] [--write]\n"
	    "\tname[=value]");
}

static void
breakdown_name(char *name, efi_guid_t *guid, char **vname)
{
	char *cp;

	cp = strrchr(name, '-');
	if (cp == NULL)
		errx(1, "Invalid name: %s", name);
	*vname = cp + 1;
	*cp = '\0';
	if (efi_name_to_guid(name, guid) < 0)
		errx(1, "Invalid guid %s", name);
}

static uint8_t *
get_value(char *val, size_t *datalen)
{
	static char buffer[16*1024];

	if (val != NULL) {
		*datalen = strlen(val);
		return ((uint8_t *)val);
	}
	/* Read from stdin */
	*datalen = sizeof(buffer);
	*datalen = read(0, buffer, *datalen);
	return ((uint8_t *)buffer);
}

static void
append_variable(char *name, char *val)
{
	char *vname;
	efi_guid_t guid;
	size_t datalen;
	uint8_t *data;

	breakdown_name(name, &guid, &vname);
	data = get_value(val, &datalen);
	if (efi_append_variable(guid, vname, data, datalen, attrib) < 0)
		err(1, "efi_append_variable");
}

static void
delete_variable(char *name)
{
	char *vname;
	efi_guid_t guid;

	breakdown_name(name, &guid, &vname);
	if (efi_del_variable(guid, vname) < 0)
		err(1, "efi_del_variable");
}

static void
write_variable(char *name, char *val)
{
	char *vname;
	efi_guid_t guid;
	size_t datalen;
	uint8_t *data;

	breakdown_name(name, &guid, &vname);
	data = get_value(val, &datalen);
	if (efi_set_variable(guid, vname, data, datalen, attrib) < 0)
		err(1, "efi_set_variable");
}

static void
devpath_dump(uint8_t *data, size_t datalen)
{
	char buffer[1024];

	efidp_format_device_path(buffer, sizeof(buffer),
	    (const_efidp)data, datalen);
	if (!Nflag)
		printf(": ");
	printf("%s\n", buffer);
}

static void
pretty_guid(efi_guid_t *guid, char **gname)
{
	char *pretty = NULL;

	if (gflag)
		efi_guid_to_name(guid, &pretty);

	if (pretty == NULL)
		efi_guid_to_str(guid, gname);
	else
		*gname = pretty;
}

static void
print_var(efi_guid_t *guid, char *name)
{
	uint32_t att;
	uint8_t *data;
	size_t datalen;
	char *gname = NULL;
	int rv;

	if (guid)
		pretty_guid(guid, &gname);
	if (pflag || fromfile) {
		if (fromfile) {
			int fd;

			fd = open(fromfile, O_RDONLY);
			if (fd < 0)
				err(1, "open %s", fromfile);
			data = malloc(64 * 1024);
			if (data == NULL)
				err(1, "malloc");
			datalen = read(fd, data, 64 * 1024);
			if (datalen <= 0)
				err(1, "read");
			close(fd);
		} else {
			rv = efi_get_variable(*guid, name, &data, &datalen, &att);
			if (rv < 0)
				err(1, "fetching %s-%s", gname, name);
		}


		if (!Nflag)
			printf("%s-%s\n", gname, name);
		if (load_opt_flag)
			efi_print_load_option(data, datalen, Aflag, bflag, uflag);
		else if (Aflag)
			asciidump(data, datalen);
		else if (uflag)
			utf8dump(data, datalen);
		else if (bflag)
			bindump(data, datalen);
		else if (dflag)
			devpath_dump(data, datalen);
		else
			hexdump(data, datalen);
	} else {
		printf("%s-%s", gname, name);
	}
	free(gname);
	if (!Nflag)
		printf("\n");
}

static void
print_variable(char *name)
{
	char *vname;
	efi_guid_t guid;

	breakdown_name(name, &guid, &vname);
	print_var(&guid, vname);
}

static void
print_variables(void)
{
	int rv;
	char *name = NULL;
	efi_guid_t *guid = NULL;

	while ((rv = efi_get_next_variable_name(&guid, &name)) > 0)
		print_var(guid, name);

	if (rv < 0)
		err(1, "Error listing names");
}

static void
print_known_guid(void)
{
	struct uuid_table *tbl;
	int i, n;

	n = efi_known_guid(&tbl);
	for (i = 0; i < n; i++)
		printf("%s %s\n", tbl[i].uuid_str, tbl[i].name);
}

static void
parse_args(int argc, char **argv)
{
	int ch, i;

	while ((ch = getopt_long(argc, argv, "aAbdDf:gHlLNn:OpRt:uw",
		    longopts, NULL)) != -1) {
		switch (ch) {
		case 'a':
			aflag++;
			break;
		case 'A':
			Aflag++;
			break;
		case 'b':
			bflag++;
			break;
		case 'd':
			dflag++;
			break;
		case 'D':
			Dflag++;
			break;
		case 'g':
			gflag++;
			break;
		case 'H':
			Hflag++;
			break;
		case 'l':
			lflag++;
			break;
		case 'L':
			Lflag++;
			break;
		case 'n':
			varname = optarg;
			break;
		case 'N':
			Nflag++;
			break;
		case 'O':
			load_opt_flag++;
			break;
		case 'p':
			pflag++;
			break;
		case 'R':
			Rflag++;
			break;
		case 't':
			attrib = strtoul(optarg, NULL, 16);
			break;
		case 'u':
			uflag++;
			break;
		case 'w':
			wflag++;
			break;
		case 'f':
			free(fromfile);
			fromfile = strdup(optarg);
			break;
		case 0:
			errx(1, "unknown or unimplemented option\n");
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 1)
		varname = argv[0];

	if (aflag + Dflag + wflag > 1) {
		warnx("Can only use one of -a (--append), "
		    "-D (--delete) and -w (--write)");
		usage();
	}

	if (aflag + Dflag + wflag > 0 && varname == NULL) {
		warnx("Must specify a variable for -a (--append), "
		    "-D (--delete) or -w (--write)");
		usage();
	}

	if (aflag)
		append_variable(varname, NULL);
	else if (Dflag)
		delete_variable(varname);
	else if (wflag)
		write_variable(varname, NULL);
	else if (Lflag)
		print_known_guid();
	else if (fromfile) {
		Nflag = 1;
		print_var(NULL, NULL);
	} else if (varname) {
		pflag++;
		print_variable(varname);
	} else if (argc > 0) {
		pflag++;
		for (i = 0; i < argc; i++)
			print_variable(argv[i]);
	} else
		print_variables();
}

int
main(int argc, char **argv)
{

	parse_args(argc, argv);
}
