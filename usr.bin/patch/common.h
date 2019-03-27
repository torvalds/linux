/*-
 * Copyright 1986, Larry Wall
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following condition is met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this condition and the following disclaimer.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * patch - a program to apply diffs to original files
 *
 * -C option added in 1998, original code by Marc Espie, based on FreeBSD
 * behaviour
 *
 * $OpenBSD: common.h,v 1.26 2006/03/11 19:41:30 otto Exp $
 * $FreeBSD$
 */

#include <sys/types.h>

#include <stdbool.h>
#include <stdint.h>

#define	DEBUGGING

/* constants */

#define	MAXHUNKSIZE 200000	/* is this enough lines? */
#define	INITHUNKMAX 125		/* initial dynamic allocation size */
#define	INITLINELEN 4096
#define	BUFFERSIZE 4096
#define	LINENUM_MAX LONG_MAX

#define	ORIGEXT ".orig"
#define	REJEXT ".rej"

/* handy definitions */

#define	strEQ(s1,s2) (strcmp(s1, s2) == 0)
#define	strnNE(s1,s2,l) (strncmp(s1, s2, l) != 0)
#define	strnEQ(s1,s2,l) (strncmp(s1, s2, l) == 0)

/* typedefs */

typedef long    LINENUM;	/* must be signed */

/* globals */

extern mode_t	filemode;

extern char	*buf;		/* general purpose buffer */		
extern size_t	buf_size;	/* size of general purpose buffer */

extern bool	using_plan_a;	/* try to keep everything in memory */
extern bool	out_of_mem;	/* ran out of memory in plan a */
extern bool	nonempty_patchf_seen;	/* seen a non-zero-length patch file? */

#define	MAXFILEC 2

extern char	*filearg[MAXFILEC];
extern bool	ok_to_create_file;
extern char	*outname;
extern char	*origprae;

extern char	*TMPOUTNAME;
extern char	*TMPINNAME;
extern char	*TMPREJNAME;
extern char	*TMPPATNAME;
extern bool	toutkeep;
extern bool	trejkeep;

#ifdef DEBUGGING
extern int	debug;
#endif

extern bool	force;
extern bool	batch;
extern bool	verbose;
extern bool	reverse;
extern bool	noreverse;
extern bool	skip_rest_of_patch;
extern int	strippath;
extern bool	canonicalize;
/* TRUE if -C was specified on command line.  */
extern bool	check_only;
extern bool	warn_on_invalid_line;
extern bool	last_line_missing_eol;


#define	CONTEXT_DIFF 1
#define	NORMAL_DIFF 2
#define	ED_DIFF 3
#define	NEW_CONTEXT_DIFF 4
#define	UNI_DIFF 5

extern int	diff_type;
extern char	*revision;	/* prerequisite revision, if any */
extern LINENUM	input_lines;	/* how long is input file in lines */

extern int	posix;

