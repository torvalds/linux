/*	$NetBSD: crunchide.c,v 1.8 1997/11/01 06:51:45 lukem Exp $	*/
/*
 * Copyright (c) 1997 Christopher G. Demetriou.  All rights reserved.
 * Copyright (c) 1994 University of Maryland
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of U.M. not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  U.M. makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * U.M. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL U.M.
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: James da Silva, Systems Design and Analysis Group
 *			   Computer Science Department
 *			   University of Maryland at College Park
 */
/*
 * crunchide.c - tiptoes through a symbol table, hiding all defined
 *	global symbols.  Allows the user to supply a "keep list" of symbols
 *	that are not to be hidden.  This program relies on the use of the
 * 	linker's -dc flag to actually put global bss data into the file's
 * 	bss segment (rather than leaving it as undefined "common" data).
 *
 * 	The point of all this is to allow multiple programs to be linked
 *	together without getting multiple-defined errors.
 *
 *	For example, consider a program "foo.c".  It can be linked with a
 *	small stub routine, called "foostub.c", eg:
 *	    int foo_main(int argc, char **argv){ return main(argc, argv); }
 *      like so:
 *	    cc -c foo.c foostub.c
 *	    ld -dc -r foo.o foostub.o -o foo.combined.o
 *	    crunchide -k _foo_main foo.combined.o
 *	at this point, foo.combined.o can be linked with another program
 * 	and invoked with "foo_main(argc, argv)".  foo's main() and any
 * 	other globals are hidden and will not conflict with other symbols.
 *
 * TODO:
 *	- resolve the theoretical hanging reloc problem (see check_reloc()
 *	  below). I have yet to see this problem actually occur in any real
 *	  program. In what cases will gcc/gas generate code that needs a
 *	  relative reloc from a global symbol, other than PIC?  The
 *	  solution is to not hide the symbol from the linker in this case,
 *	  but to generate some random name for it so that it doesn't link
 *	  with anything but holds the place for the reloc.
 *      - arrange that all the BSS segments start at the same address, so
 *	  that the final crunched binary BSS size is the max of all the
 *	  component programs' BSS sizes, rather than their sum.
 */

#include <sys/cdefs.h>
#ifndef lint
__RCSID("$NetBSD: crunchide.c,v 1.8 1997/11/01 06:51:45 lukem Exp $");
#endif
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "extern.h"

char *pname = "crunchide";

void usage(void);

void add_to_keep_list(char *symbol);
void add_file_to_keep_list(char *filename);

int hide_syms(const char *filename);

int verbose;

int main(int, char *[]);

int
main(int argc, char **argv)
{
    int ch, errors;

    if(argc > 0) pname = argv[0];

    while ((ch = getopt(argc, argv, "k:f:v")) != -1)
	switch(ch) {
	case 'k':
	    add_to_keep_list(optarg);
	    break;
	case 'f':
	    add_file_to_keep_list(optarg);
	    break;
	case 'v':
	    verbose = 1;
	    break;
	default:
	    usage();
	}

    argc -= optind;
    argv += optind;

    if(argc == 0) usage();

    errors = 0;
    while(argc) {
	if (hide_syms(*argv))
		errors = 1;
	argc--, argv++;
    }

    return errors;
}

void
usage(void)
{
    fprintf(stderr,
	    "usage: %s [-k <symbol-name>] [-f <keep-list-file>] <files> ...\n",
	    pname);
    exit(1);
}

/* ---------------------------- */

struct keep {
    struct keep *next;
    char *sym;
} *keep_list;

void
add_to_keep_list(char *symbol)
{
    struct keep *newp, *prevp, *curp;
    int cmp;

    cmp = 0;

    for(curp = keep_list, prevp = NULL; curp; prevp = curp, curp = curp->next)
	if((cmp = strcmp(symbol, curp->sym)) <= 0) break;

    if(curp && cmp == 0)
	return;	/* already in table */

    newp = (struct keep *) malloc(sizeof(struct keep));
    if(newp) newp->sym = strdup(symbol);
    if(newp == NULL || newp->sym == NULL) {
	fprintf(stderr, "%s: out of memory for keep list\n", pname);
	exit(1);
    }

    newp->next = curp;
    if(prevp) prevp->next = newp;
    else keep_list = newp;
}

int
in_keep_list(const char *symbol)
{
    struct keep *curp;
    int cmp;

    cmp = 0;

    for(curp = keep_list; curp; curp = curp->next)
	if((cmp = strcmp(symbol, curp->sym)) <= 0) break;

    return curp && cmp == 0;
}

void
add_file_to_keep_list(char *filename)
{
    FILE *keepf;
    char symbol[1024];
    int len;

    if((keepf = fopen(filename, "r")) == NULL) {
	perror(filename);
	usage();
    }

    while(fgets(symbol, sizeof(symbol), keepf)) {
	len = strlen(symbol);
	if(len && symbol[len-1] == '\n')
	    symbol[len-1] = '\0';

	add_to_keep_list(symbol);
    }
    fclose(keepf);
}

/* ---------------------------- */

struct {
	const char *name;
	int	(*check)(int, const char *);	/* 1 if match, zero if not */
	int	(*hide)(int, const char *);	/* non-zero if error */
} exec_formats[] = {
#ifdef NLIST_ELF32
	{	"ELF32",	check_elf32,	hide_elf32,	},
#endif
#ifdef NLIST_ELF64
	{	"ELF64",	check_elf64,	hide_elf64,	},
#endif
};

int
hide_syms(const char *filename)
{
	int fd, i, n, rv;

	fd = open(filename, O_RDWR, 0);
	if (fd == -1) {
		perror(filename);
		return 1;
	}

	rv = 0;

        n = sizeof exec_formats / sizeof exec_formats[0];
        for (i = 0; i < n; i++) {
		if (lseek(fd, 0, SEEK_SET) != 0) {
			perror(filename);
			goto err;
		}
                if ((*exec_formats[i].check)(fd, filename) != 0)
                        break;
	}
	if (i == n) {
		fprintf(stderr, "%s: unknown executable format\n", filename);
		goto err;
	}

	if (verbose)
		fprintf(stderr, "%s is an %s binary\n", filename,
		    exec_formats[i].name);

	if (lseek(fd, 0, SEEK_SET) != 0) {
		perror(filename);
		goto err;
	}
	rv = (*exec_formats[i].hide)(fd, filename);

out:
	close (fd);
	return (rv);

err:
	rv = 1;
	goto out;
}
