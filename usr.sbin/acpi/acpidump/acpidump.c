/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Mitsuru IWASAKI <iwasaki@FreeBSD.org>
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
 *	$FreeBSD$
 */

#include <sys/param.h>
#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "acpidump.h"

int	dflag;	/* Disassemble AML using iasl(8) */
int	tflag;	/* Dump contents of SDT tables */
int	vflag;	/* Use verbose messages */

static void
usage(const char *progname)
{

	fprintf(stderr, "usage: %s [-d] [-t] [-h] [-v] [-f dsdt_input] "
			"[-o dsdt_output]\n", progname);
	fprintf(stderr, "To send ASL:\n\t%s -dt | gzip -c9 > foo.asl.gz\n",
	    progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	ACPI_TABLE_HEADER *rsdt, *sdt;
	int	c;
	char	*progname;
	char	*dsdt_input_file, *dsdt_output_file;

	dsdt_input_file = dsdt_output_file = NULL;
	progname = argv[0];

	if (argc < 2)
		usage(progname);

	while ((c = getopt(argc, argv, "dhtvf:o:")) != -1) {
		switch (c) {
		case 'd':
			dflag = 1;
			break;
		case 't':
			tflag = 1;
			break;
		case 'v':
			vflag = 1;
			break;
		case 'f':
			dsdt_input_file = optarg;
			break;
		case 'o':
			dsdt_output_file = optarg;
			break;
		case 'h':
		default:
			usage(progname);
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	/* Get input either from file or /dev/mem */
	if (dsdt_input_file != NULL) {
		if (dflag == 0 && tflag == 0) {
			warnx("Need to specify -d or -t with DSDT input file");
			usage(progname);
		} else if (tflag != 0) {
			warnx("Can't use -t with DSDT input file");
			usage(progname);
		}
		if (vflag)
			warnx("loading DSDT file: %s", dsdt_input_file);
		rsdt = dsdt_load_file(dsdt_input_file);
	} else {
		if (vflag)
			warnx("loading RSD PTR from /dev/mem");
		rsdt = sdt_load_devmem();
	}

	/* Display misc. SDT tables (only available when using /dev/mem) */
	if (tflag) {
		if (vflag)
			warnx("printing various SDT tables");
		sdt_print_all(rsdt);
	}

	/* Translate RSDT to DSDT pointer */
	if (dsdt_input_file == NULL) {
		sdt = sdt_from_rsdt(rsdt, ACPI_SIG_FADT, NULL);
		sdt = dsdt_from_fadt((ACPI_TABLE_FADT *)sdt);
	} else {
		sdt = rsdt;
		rsdt = NULL;
	}

	/* Dump the DSDT and SSDTs to a file */
	if (dsdt_output_file != NULL) {
		if (vflag)
			warnx("saving DSDT file: %s", dsdt_output_file);
		dsdt_save_file(dsdt_output_file, rsdt, sdt);
	}

	/* Disassemble the DSDT into ASL */
	if (dflag) {
		if (vflag)
			warnx("disassembling DSDT, iasl messages follow");
		aml_disassemble(rsdt, sdt);
		if (vflag)
			warnx("iasl processing complete");
	}

	exit(0);
}
