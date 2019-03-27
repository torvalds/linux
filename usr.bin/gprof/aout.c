/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if 0
/* From: */
#ifndef lint
static char sccsid[] = "@(#)gprof.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <netinet/in.h>

#include <a.out.h>
#include <err.h>
#include <string.h>

#include "gprof.h"

static void getstrtab(FILE *, const char *);
static void getsymtab(FILE *, const char *);
static void gettextspace(FILE *);
static bool funcsymbol(struct nlist *);

static char	*strtab;		/* string table in core */
static long	ssiz;			/* size of the string table */
static struct	exec xbuf;		/* exec header of a.out */

/* Things which get -E excluded by default. */
static char	*excludes[] = { "mcount", "__mcleanup", NULL };

    /*
     * Set up string and symbol tables from a.out.
     *	and optionally the text space.
     * On return symbol table is sorted by value.
     *
     * Returns 0 on success, -1 on failure.
     */
int
aout_getnfile(const char *filename, char ***defaultEs)
{
    FILE	*nfile;

    nfile = fopen( filename ,"r");
    if (nfile == NULL)
	err( 1 , "%s", filename );
    fread(&xbuf, 1, sizeof(xbuf), nfile);
    if (N_BADMAG(xbuf)) {
	fclose(nfile);
	return -1;
    }
    getstrtab(nfile, filename);
    getsymtab(nfile, filename);
    gettextspace( nfile );
    fclose(nfile);
#   ifdef DEBUG
	if ( debug & AOUTDEBUG ) {
	    register int j;

	    for (j = 0; j < nname; j++){
		printf("[getnfile] 0X%08lx\t%s\n", nl[j].value, nl[j].name);
	    }
	}
#   endif /* DEBUG */
    *defaultEs = excludes;
    return 0;
}

static void
getstrtab(FILE *nfile, const char *filename)
{

    fseek(nfile, (long)(N_SYMOFF(xbuf) + xbuf.a_syms), 0);
    if (fread(&ssiz, sizeof (ssiz), 1, nfile) == 0)
	errx( 1 , "%s: no string table (old format?)" , filename );
    strtab = calloc(ssiz, 1);
    if (strtab == NULL)
	errx( 1 , "%s: no room for %ld bytes of string table", filename , ssiz);
    if (fread(strtab+sizeof(ssiz), ssiz-sizeof(ssiz), 1, nfile) != 1)
	errx( 1 , "%s: error reading string table" , filename );
}

    /*
     * Read in symbol table
     */
static void
getsymtab(FILE *nfile, const char *filename)
{
    register long	i;
    int			askfor;
    struct nlist	nbuf;

    /* pass1 - count symbols */
    fseek(nfile, (long)N_SYMOFF(xbuf), 0);
    nname = 0;
    for (i = xbuf.a_syms; i > 0; i -= sizeof(struct nlist)) {
	fread(&nbuf, sizeof(nbuf), 1, nfile);
	if ( ! funcsymbol( &nbuf ) ) {
	    continue;
	}
	nname++;
    }
    if (nname == 0)
	errx( 1 , "%s: no symbols" , filename );
    askfor = nname + 1;
    nl = (nltype *) calloc( askfor , sizeof(nltype) );
    if (nl == NULL)
	errx( 1 , "no room for %zu bytes of symbol table" ,
		askfor * sizeof(nltype) );

    /* pass2 - read symbols */
    fseek(nfile, (long)N_SYMOFF(xbuf), 0);
    npe = nl;
    nname = 0;
    for (i = xbuf.a_syms; i > 0; i -= sizeof(struct nlist)) {
	fread(&nbuf, sizeof(nbuf), 1, nfile);
	if ( ! funcsymbol( &nbuf ) ) {
#	    ifdef DEBUG
		if ( debug & AOUTDEBUG ) {
		    printf( "[getsymtab] rejecting: 0x%x %s\n" ,
			    nbuf.n_type , strtab + nbuf.n_un.n_strx );
		}
#	    endif /* DEBUG */
	    continue;
	}
	npe->value = nbuf.n_value;
	npe->name = strtab+nbuf.n_un.n_strx;
#	ifdef DEBUG
	    if ( debug & AOUTDEBUG ) {
		printf( "[getsymtab] %d %s 0x%08lx\n" ,
			nname , npe -> name , npe -> value );
	    }
#	endif /* DEBUG */
	npe++;
	nname++;
    }
    npe->value = -1;
}

    /*
     *	read in the text space of an a.out file
     */
static void
gettextspace(FILE *nfile)
{

    textspace = (u_char *) malloc( xbuf.a_text );
    if ( textspace == NULL ) {
	warnx("no room for %u bytes of text space: can't do -c" ,
		  xbuf.a_text );
	return;
    }
    (void) fseek( nfile , N_TXTOFF( xbuf ) , 0 );
    if ( fread( textspace , 1 , xbuf.a_text , nfile ) != xbuf.a_text ) {
	warnx("couldn't read text space: can't do -c");
	free( textspace );
	textspace = 0;
	return;
    }
}

static bool
funcsymbol(struct nlist *nlistp)
{
    char	*name, c;

	/*
	 *	must be a text symbol,
	 *	and static text symbols don't qualify if aflag set.
	 */
    if ( ! (  ( nlistp -> n_type == ( N_TEXT | N_EXT ) )
	   || ( ( nlistp -> n_type == N_TEXT ) && ( aflag == 0 ) ) ) ) {
	return FALSE;
    }
	/*
	 *	name must start with an underscore if uflag is set.
	 *	can't have any `funny' characters in name,
	 *	where `funny' means `.' (.o file names)
	 *	need to make an exception for sparc .mul & co.
	 *	perhaps we should just drop this code entirely...
	 */
    name = strtab + nlistp -> n_un.n_strx;
    if ( uflag && *name != '_' )
	return FALSE;
#ifdef sparc
    if ( *name == '.' ) {
	char *p = name + 1;
	if ( *p == 'u' )
	    p++;
	if ( strcmp ( p, "mul" ) == 0 || strcmp ( p, "div" ) == 0 ||
	     strcmp ( p, "rem" ) == 0 )
		return TRUE;
    }
#endif
    while ( (c = *name++) ) {
	if ( c == '.' ) {
	    return FALSE;
	}
    }
    return TRUE;
}
