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
#ifndef lint
static char sccsid[] = "@(#)dfn.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include "gprof.h"

#define	DFN_DEPTH	100
struct dfnstruct {
    nltype	*nlentryp;
    int		cycletop;
};
typedef struct dfnstruct	dfntype;

dfntype	dfn_stack[ DFN_DEPTH ];
int	dfn_depth;

int	dfn_counter;

void
dfn_init(void)
{

    dfn_depth = 0;
    dfn_counter = DFN_NAN;
}

    /*
     *	given this parent, depth first number its children.
     */
void
dfn(nltype *parentp)
{
    arctype	*arcp;

#   ifdef DEBUG
	if ( debug & DFNDEBUG ) {
	    printf( "[dfn] dfn(" );
	    printname( parentp );
	    printf( ")\n" );
	}
#   endif /* DEBUG */
	/*
	 *	if we're already numbered, no need to look any further.
	 */
    if ( dfn_numbered( parentp ) ) {
	return;
    }
	/*
	 *	if we're already busy, must be a cycle
	 */
    if ( dfn_busy( parentp ) ) {
	dfn_findcycle( parentp );
	return;
    }
	/*
	 *	visit yourself before your children
	 */
    dfn_pre_visit( parentp );
	/*
	 *	visit children
	 */
    for ( arcp = parentp -> children ; arcp ; arcp = arcp -> arc_childlist ) {
	    if ( arcp -> arc_flags & DEADARC )
		continue;
	    dfn( arcp -> arc_childp );
    }
	/*
	 *	visit yourself after your children
	 */
    dfn_post_visit( parentp );
}

    /*
     *	push a parent onto the stack and mark it busy
     */
void
dfn_pre_visit(nltype *parentp)
{

    dfn_depth += 1;
    if ( dfn_depth >= DFN_DEPTH )
	errx( 1 , "[dfn] out of my depth (dfn_stack overflow)" );
    dfn_stack[ dfn_depth ].nlentryp = parentp;
    dfn_stack[ dfn_depth ].cycletop = dfn_depth;
    parentp -> toporder = DFN_BUSY;
#   ifdef DEBUG
	if ( debug & DFNDEBUG ) {
	    printf( "[dfn_pre_visit]\t\t%d:" , dfn_depth );
	    printname( parentp );
	    printf( "\n" );
	}
#   endif /* DEBUG */
}

    /*
     *	are we already numbered?
     */
bool
dfn_numbered(nltype *childp)
{

    return ( childp -> toporder != DFN_NAN && childp -> toporder != DFN_BUSY );
}

    /*
     *	are we already busy?
     */
bool
dfn_busy(nltype *childp)
{

    if ( childp -> toporder == DFN_NAN ) {
	return FALSE;
    }
    return TRUE;
}

    /*
     *	MISSING: an explanation
     */
void
dfn_findcycle(nltype *childp)
{
    int		cycletop;
    nltype	*cycleheadp;
    nltype	*tailp;
    int		index;

    for ( cycletop = dfn_depth ; cycletop > 0 ; cycletop -= 1 ) {
	cycleheadp = dfn_stack[ cycletop ].nlentryp;
	if ( childp == cycleheadp ) {
	    break;
	}
	if ( childp -> cyclehead != childp &&
	    childp -> cyclehead == cycleheadp ) {
	    break;
	}
    }
    if ( cycletop <= 0 )
	errx( 1 , "[dfn_findcycle] couldn't find head of cycle" );
#   ifdef DEBUG
	if ( debug & DFNDEBUG ) {
	    printf( "[dfn_findcycle] dfn_depth %d cycletop %d " ,
		    dfn_depth , cycletop  );
	    printname( cycleheadp );
	    printf( "\n" );
	}
#   endif /* DEBUG */
    if ( cycletop == dfn_depth ) {
	    /*
	     *	this is previous function, e.g. this calls itself
	     *	sort of boring
	     */
	dfn_self_cycle( childp );
    } else {
	    /*
	     *	glom intervening functions that aren't already
	     *	glommed into this cycle.
	     *	things have been glommed when their cyclehead field
	     *	points to the head of the cycle they are glommed into.
	     */
	for ( tailp = cycleheadp ; tailp -> cnext ; tailp = tailp -> cnext ) {
	    /* void: chase down to tail of things already glommed */
#	    ifdef DEBUG
		if ( debug & DFNDEBUG ) {
		    printf( "[dfn_findcycle] tail " );
		    printname( tailp );
		    printf( "\n" );
		}
#	    endif /* DEBUG */
	}
	    /*
	     *	if what we think is the top of the cycle
	     *	has a cyclehead field, then it's not really the
	     *	head of the cycle, which is really what we want
	     */
	if ( cycleheadp -> cyclehead != cycleheadp ) {
	    cycleheadp = cycleheadp -> cyclehead;
#	    ifdef DEBUG
		if ( debug & DFNDEBUG ) {
		    printf( "[dfn_findcycle] new cyclehead " );
		    printname( cycleheadp );
		    printf( "\n" );
		}
#	    endif /* DEBUG */
	}
	for ( index = cycletop + 1 ; index <= dfn_depth ; index += 1 ) {
	    childp = dfn_stack[ index ].nlentryp;
	    if ( childp -> cyclehead == childp ) {
		    /*
		     *	not yet glommed anywhere, glom it
		     *	and fix any children it has glommed
		     */
		tailp -> cnext = childp;
		childp -> cyclehead = cycleheadp;
#		ifdef DEBUG
		    if ( debug & DFNDEBUG ) {
			printf( "[dfn_findcycle] glomming " );
			printname( childp );
			printf( " onto " );
			printname( cycleheadp );
			printf( "\n" );
		    }
#		endif /* DEBUG */
		for ( tailp = childp ; tailp->cnext ; tailp = tailp->cnext ) {
		    tailp -> cnext -> cyclehead = cycleheadp;
#		    ifdef DEBUG
			if ( debug & DFNDEBUG ) {
			    printf( "[dfn_findcycle] and its tail " );
			    printname( tailp -> cnext );
			    printf( " onto " );
			    printname( cycleheadp );
			    printf( "\n" );
			}
#		    endif /* DEBUG */
		}
	    } else if ( childp -> cyclehead != cycleheadp /* firewall */ ) {
		fprintf( stderr ,
			"[dfn_busy] glommed, but not to cyclehead\n" );
	    }
	}
    }
}

    /*
     *	deal with self-cycles
     *	for lint: ARGSUSED
     */
void
dfn_self_cycle(nltype *parentp)
{
	/*
	 *	since we are taking out self-cycles elsewhere
	 *	no need for the special case, here.
	 */
#   ifdef DEBUG
	if ( debug & DFNDEBUG ) {
	    printf( "[dfn_self_cycle] " );
	    printname( parentp );
	    printf( "\n" );
	}
#   endif /* DEBUG */
}

    /*
     *	visit a node after all its children
     *	[MISSING: an explanation]
     *	and pop it off the stack
     */
void
dfn_post_visit(nltype *parentp)
{
    nltype	*memberp;

#   ifdef DEBUG
	if ( debug & DFNDEBUG ) {
	    printf( "[dfn_post_visit]\t%d: " , dfn_depth );
	    printname( parentp );
	    printf( "\n" );
	}
#   endif /* DEBUG */
	/*
	 *	number functions and things in their cycles
	 *	unless the function is itself part of a cycle
	 */
    if ( parentp -> cyclehead == parentp ) {
	dfn_counter += 1;
	for ( memberp = parentp ; memberp ; memberp = memberp -> cnext ) {
	    memberp -> toporder = dfn_counter;
#	    ifdef DEBUG
		if ( debug & DFNDEBUG ) {
		    printf( "[dfn_post_visit]\t\tmember " );
		    printname( memberp );
		    printf( " -> toporder = %d\n" , dfn_counter );
		}
#	    endif /* DEBUG */
	}
    } else {
#	ifdef DEBUG
	    if ( debug & DFNDEBUG ) {
		printf( "[dfn_post_visit]\t\tis part of a cycle\n" );
	    }
#	endif /* DEBUG */
    }
    dfn_depth -= 1;
}
