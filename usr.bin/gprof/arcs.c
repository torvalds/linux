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
static char sccsid[] = "@(#)arcs.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include "gprof.h"

#ifdef DEBUG
int visited;
int viable;
int newcycle;
int oldcycle;
#endif /* DEBUG */

int topcmp(const void *, const void *);

    /*
     *	add (or just increment) an arc
     */
void
addarc(nltype *parentp, nltype *childp, long count)
{
    arctype		*arcp;

#   ifdef DEBUG
	if ( debug & TALLYDEBUG ) {
	    printf( "[addarc] %ld arcs from %s to %s\n" ,
		    count , parentp -> name , childp -> name );
	}
#   endif /* DEBUG */
    arcp = arclookup( parentp , childp );
    if ( arcp != 0 ) {
	    /*
	     *	a hit:  just increment the count.
	     */
#	ifdef DEBUG
	    if ( debug & TALLYDEBUG ) {
		printf( "[tally] hit %ld += %ld\n" ,
			arcp -> arc_count , count );
	    }
#	endif /* DEBUG */
	arcp -> arc_count += count;
	return;
    }
    arcp = (arctype *)calloc( 1 , sizeof *arcp );
    if (arcp == NULL)
	errx( 1 , "malloc failed" );
    arcp -> arc_parentp = parentp;
    arcp -> arc_childp = childp;
    arcp -> arc_count = count;
	/*
	 *	prepend this child to the children of this parent
	 */
    arcp -> arc_childlist = parentp -> children;
    parentp -> children = arcp;
	/*
	 *	prepend this parent to the parents of this child
	 */
    arcp -> arc_parentlist = childp -> parents;
    childp -> parents = arcp;
}

    /*
     *	the code below topologically sorts the graph (collapsing cycles),
     *	and propagates time bottom up and flags top down.
     */

    /*
     *	the topologically sorted name list pointers
     */
nltype	**topsortnlp;

int
topcmp(const void *v1, const void *v2)
{
    const nltype **npp1 = (const nltype **)v1;
    const nltype **npp2 = (const nltype **)v2;

    return (*npp1) -> toporder - (*npp2) -> toporder;
}

nltype **
doarcs(void)
{
    nltype	*parentp, **timesortnlp;
    arctype	*arcp;
    long	index;
    long	pass;

	/*
	 *	initialize various things:
	 *	    zero out child times.
	 *	    count self-recursive calls.
	 *	    indicate that nothing is on cycles.
	 */
    for ( parentp = nl ; parentp < npe ; parentp++ ) {
	parentp -> childtime = 0.0;
	arcp = arclookup( parentp , parentp );
	if ( arcp != 0 ) {
	    parentp -> ncall -= arcp -> arc_count;
	    parentp -> selfcalls = arcp -> arc_count;
	} else {
	    parentp -> selfcalls = 0;
	}
	parentp -> npropcall = parentp -> ncall;
	parentp -> propfraction = 0.0;
	parentp -> propself = 0.0;
	parentp -> propchild = 0.0;
	parentp -> printflag = FALSE;
	parentp -> toporder = DFN_NAN;
	parentp -> cycleno = 0;
	parentp -> cyclehead = parentp;
	parentp -> cnext = 0;
    }
    for ( pass = 1 ; ; pass++ ) {
	    /*
	     *	topologically order things
	     *	if any node is unnumbered,
	     *	    number it and any of its descendents.
	     */
	for ( dfn_init() , parentp = nl ; parentp < npe ; parentp++ ) {
	    if ( parentp -> toporder == DFN_NAN ) {
		dfn( parentp );
	    }
	}
	    /*
	     *	link together nodes on the same cycle
	     */
	cyclelink();
	    /*
	     *	if no cycles to break up, proceed
	     */
	if ( ! Cflag )
	    break;
	    /*
	     *	analyze cycles to determine breakup
	     */
#	ifdef DEBUG
	    if ( debug & BREAKCYCLE ) {
		printf("[doarcs] pass %ld, cycle(s) %d\n" , pass , ncycle );
	    }
#	endif /* DEBUG */
	if ( pass == 1 ) {
	    printf( "\n\n%s %s\n%s %d:\n" ,
		"The following arcs were deleted" ,
		"from the propagation calculation" ,
		"to reduce the maximum cycle size to", cyclethreshold );
	}
	if ( cycleanalyze() )
	    break;
	free ( cyclenl );
	ncycle = 0;
	for ( parentp = nl ; parentp < npe ; parentp++ ) {
	    parentp -> toporder = DFN_NAN;
	    parentp -> cycleno = 0;
	    parentp -> cyclehead = parentp;
	    parentp -> cnext = 0;
	}
    }
    if ( pass > 1 ) {
	printf( "\f\n" );
    } else {
	printf( "\tNone\n\n" );
    }
	/*
	 *	Sort the symbol table in reverse topological order
	 */
    topsortnlp = (nltype **) calloc( nname , sizeof(nltype *) );
    if ( topsortnlp == (nltype **) 0 )
	errx( 1 , "[doarcs] ran out of memory for topo sorting" );
    for ( index = 0 ; index < nname ; index += 1 ) {
	topsortnlp[ index ] = &nl[ index ];
    }
    qsort( topsortnlp , nname , sizeof(nltype *) , topcmp );
#   ifdef DEBUG
	if ( debug & DFNDEBUG ) {
	    printf( "[doarcs] topological sort listing\n" );
	    for ( index = 0 ; index < nname ; index += 1 ) {
		printf( "[doarcs] " );
		printf( "%d:" , topsortnlp[ index ] -> toporder );
		printname( topsortnlp[ index ] );
		printf( "\n" );
	    }
	}
#   endif /* DEBUG */
	/*
	 *	starting from the topological top,
	 *	propagate print flags to children.
	 *	also, calculate propagation fractions.
	 *	this happens before time propagation
	 *	since time propagation uses the fractions.
	 */
    doflags();
	/*
	 *	starting from the topological bottom,
	 *	propagate children times up to parents.
	 */
    dotime();
	/*
	 *	Now, sort by propself + propchild.
	 *	sorting both the regular function names
	 *	and cycle headers.
	 */
    timesortnlp = (nltype **) calloc( nname + ncycle , sizeof(nltype *) );
    if ( timesortnlp == (nltype **) 0 )
	errx( 1 , "ran out of memory for sorting" );
    for ( index = 0 ; index < nname ; index++ ) {
	timesortnlp[index] = &nl[index];
    }
    for ( index = 1 ; index <= ncycle ; index++ ) {
	timesortnlp[nname+index-1] = &cyclenl[index];
    }
    qsort( timesortnlp , nname + ncycle , sizeof(nltype *) , totalcmp );
    for ( index = 0 ; index < nname + ncycle ; index++ ) {
	timesortnlp[ index ] -> index = index + 1;
    }
    return( timesortnlp );
}

void
dotime(void)
{
    int	index;

    cycletime();
    for ( index = 0 ; index < nname ; index += 1 ) {
	timepropagate( topsortnlp[ index ] );
    }
}

void
timepropagate(nltype *parentp)
{
    arctype	*arcp;
    nltype	*childp;
    double	share;
    double	propshare;

    if ( parentp -> propfraction == 0.0 ) {
	return;
    }
	/*
	 *	gather time from children of this parent.
	 */
    for ( arcp = parentp -> children ; arcp ; arcp = arcp -> arc_childlist ) {
	childp = arcp -> arc_childp;
	if ( arcp -> arc_flags & DEADARC ) {
	    continue;
	}
	if ( arcp -> arc_count == 0 ) {
	    continue;
	}
	if ( childp == parentp ) {
	    continue;
	}
	if ( childp -> propfraction == 0.0 ) {
	    continue;
	}
	if ( childp -> cyclehead != childp ) {
	    if ( parentp -> cycleno == childp -> cycleno ) {
		continue;
	    }
	    if ( parentp -> toporder <= childp -> toporder ) {
		fprintf( stderr , "[propagate] toporder botches\n" );
	    }
	    childp = childp -> cyclehead;
	} else {
	    if ( parentp -> toporder <= childp -> toporder ) {
		fprintf( stderr , "[propagate] toporder botches\n" );
		continue;
	    }
	}
	if ( childp -> npropcall == 0 ) {
	    continue;
	}
	    /*
	     *	distribute time for this arc
	     */
	arcp -> arc_time = childp -> time
			        * ( ( (double) arcp -> arc_count ) /
				    ( (double) childp -> npropcall ) );
	arcp -> arc_childtime = childp -> childtime
			        * ( ( (double) arcp -> arc_count ) /
				    ( (double) childp -> npropcall ) );
	share = arcp -> arc_time + arcp -> arc_childtime;
	parentp -> childtime += share;
	    /*
	     *	( 1 - propfraction ) gets lost along the way
	     */
	propshare = parentp -> propfraction * share;
	    /*
	     *	fix things for printing
	     */
	parentp -> propchild += propshare;
	arcp -> arc_time *= parentp -> propfraction;
	arcp -> arc_childtime *= parentp -> propfraction;
	    /*
	     *	add this share to the parent's cycle header, if any.
	     */
	if ( parentp -> cyclehead != parentp ) {
	    parentp -> cyclehead -> childtime += share;
	    parentp -> cyclehead -> propchild += propshare;
	}
#	ifdef DEBUG
	    if ( debug & PROPDEBUG ) {
		printf( "[dotime] child \t" );
		printname( childp );
		printf( " with %f %f %ld/%ld\n" ,
			childp -> time , childp -> childtime ,
			arcp -> arc_count , childp -> npropcall );
		printf( "[dotime] parent\t" );
		printname( parentp );
		printf( "\n[dotime] share %f\n" , share );
	    }
#	endif /* DEBUG */
    }
}

void
cyclelink(void)
{
    register nltype	*nlp;
    register nltype	*cyclenlp;
    int			cycle;
    nltype		*memberp;
    arctype		*arcp;

	/*
	 *	Count the number of cycles, and initialize the cycle lists
	 */
    ncycle = 0;
    for ( nlp = nl ; nlp < npe ; nlp++ ) {
	    /*
	     *	this is how you find unattached cycles
	     */
	if ( nlp -> cyclehead == nlp && nlp -> cnext != 0 ) {
	    ncycle += 1;
	}
    }
	/*
	 *	cyclenl is indexed by cycle number:
	 *	i.e. it is origin 1, not origin 0.
	 */
    cyclenl = (nltype *) calloc( ncycle + 1 , sizeof( nltype ) );
    if ( cyclenl == NULL )
	errx( 1 , "no room for %zu bytes of cycle headers" ,
		   ( ncycle + 1 ) * sizeof( nltype ) );
	/*
	 *	now link cycles to true cycleheads,
	 *	number them, accumulate the data for the cycle
	 */
    cycle = 0;
    for ( nlp = nl ; nlp < npe ; nlp++ ) {
	if ( !( nlp -> cyclehead == nlp && nlp -> cnext != 0 ) ) {
	    continue;
	}
	cycle += 1;
	cyclenlp = &cyclenl[cycle];
        cyclenlp -> name = 0;		/* the name */
        cyclenlp -> value = 0;		/* the pc entry point */
        cyclenlp -> time = 0.0;		/* ticks in this routine */
        cyclenlp -> childtime = 0.0;	/* cumulative ticks in children */
	cyclenlp -> ncall = 0;		/* how many times called */
	cyclenlp -> selfcalls = 0;	/* how many calls to self */
	cyclenlp -> propfraction = 0.0;	/* what % of time propagates */
	cyclenlp -> propself = 0.0;	/* how much self time propagates */
	cyclenlp -> propchild = 0.0;	/* how much child time propagates */
	cyclenlp -> printflag = TRUE;	/* should this be printed? */
	cyclenlp -> index = 0;		/* index in the graph list */
	cyclenlp -> toporder = DFN_NAN;	/* graph call chain top-sort order */
	cyclenlp -> cycleno = cycle;	/* internal number of cycle on */
	cyclenlp -> cyclehead = cyclenlp;	/* pointer to head of cycle */
	cyclenlp -> cnext = nlp;	/* pointer to next member of cycle */
	cyclenlp -> parents = 0;	/* list of caller arcs */
	cyclenlp -> children = 0;	/* list of callee arcs */
#	ifdef DEBUG
	    if ( debug & CYCLEDEBUG ) {
		printf( "[cyclelink] " );
		printname( nlp );
		printf( " is the head of cycle %d\n" , cycle );
	    }
#	endif /* DEBUG */
	    /*
	     *	link members to cycle header
	     */
	for ( memberp = nlp ; memberp ; memberp = memberp -> cnext ) {
	    memberp -> cycleno = cycle;
	    memberp -> cyclehead = cyclenlp;
	}
	    /*
	     *	count calls from outside the cycle
	     *	and those among cycle members
	     */
	for ( memberp = nlp ; memberp ; memberp = memberp -> cnext ) {
	    for ( arcp=memberp->parents ; arcp ; arcp=arcp->arc_parentlist ) {
		if ( arcp -> arc_parentp == memberp ) {
		    continue;
		}
		if ( arcp -> arc_parentp -> cycleno == cycle ) {
		    cyclenlp -> selfcalls += arcp -> arc_count;
		} else {
		    cyclenlp -> npropcall += arcp -> arc_count;
		}
	    }
	}
    }
}

    /*
     *	analyze cycles to determine breakup
     */
bool
cycleanalyze(void)
{
    arctype	**cyclestack;
    arctype	**stkp;
    arctype	**arcpp;
    arctype	**endlist;
    arctype	*arcp;
    nltype	*nlp;
    cltype	*clp;
    bool	ret;
    bool	done;
    int		size;
    int		cycleno;

	/*
	 *	calculate the size of the cycle, and find nodes that
	 *	exit the cycle as they are desirable targets to cut
	 *	some of their parents
	 */
    for ( done = TRUE , cycleno = 1 ; cycleno <= ncycle ; cycleno++ ) {
	size = 0;
	for (nlp = cyclenl[ cycleno ] . cnext; nlp; nlp = nlp -> cnext) {
	    size += 1;
	    nlp -> parentcnt = 0;
	    nlp -> flags &= ~HASCYCLEXIT;
	    for ( arcp = nlp -> parents; arcp; arcp = arcp -> arc_parentlist ) {
		nlp -> parentcnt += 1;
		if ( arcp -> arc_parentp -> cycleno != cycleno )
		    nlp -> flags |= HASCYCLEXIT;
	    }
	}
	if ( size <= cyclethreshold )
	    continue;
	done = FALSE;
        cyclestack = (arctype **) calloc( size + 1 , sizeof( arctype *) );
	if ( cyclestack == NULL )
	    errx( 1, "no room for %zu bytes of cycle stack" ,
			   ( size + 1 ) * sizeof( arctype * ) );
#	ifdef DEBUG
	    if ( debug & BREAKCYCLE ) {
		printf( "[cycleanalyze] starting cycle %d of %d, size %d\n" ,
		    cycleno , ncycle , size );
	    }
#	endif /* DEBUG */
	for ( nlp = cyclenl[ cycleno ] . cnext ; nlp ; nlp = nlp -> cnext ) {
	    stkp = &cyclestack[0];
	    nlp -> flags |= CYCLEHEAD;
	    ret = descend ( nlp , cyclestack , stkp );
	    nlp -> flags &= ~CYCLEHEAD;
	    if ( ret == FALSE )
		break;
	}
	free( cyclestack );
	if ( cyclecnt > 0 ) {
	    compresslist();
	    for ( clp = cyclehead ; clp ; ) {
		endlist = &clp -> list[ clp -> size ];
		for ( arcpp = clp -> list ; arcpp < endlist ; arcpp++ )
		    (*arcpp) -> arc_cyclecnt--;
		cyclecnt--;
		clp = clp -> next;
		free( clp );
	    }
	    cyclehead = 0;
	}
    }
#   ifdef DEBUG
	if ( debug & BREAKCYCLE ) {
	    printf("%s visited %d, viable %d, newcycle %d, oldcycle %d\n",
		"[doarcs]" , visited , viable , newcycle , oldcycle);
	}
#   endif /* DEBUG */
    return( done );
}

bool
descend(nltype *node, arctype **stkstart, arctype **stkp)
{
    arctype	*arcp;
    bool	ret;

    for ( arcp = node -> children ; arcp ; arcp = arcp -> arc_childlist ) {
#	ifdef DEBUG
	    visited++;
#	endif /* DEBUG */
	if ( arcp -> arc_childp -> cycleno != node -> cycleno
	    || ( arcp -> arc_childp -> flags & VISITED )
	    || ( arcp -> arc_flags & DEADARC ) )
	    continue;
#	ifdef DEBUG
	    viable++;
#	endif /* DEBUG */
	*stkp = arcp;
	if ( arcp -> arc_childp -> flags & CYCLEHEAD ) {
	    if ( addcycle( stkstart , stkp ) == FALSE )
		return( FALSE );
	    continue;
	}
	arcp -> arc_childp -> flags |= VISITED;
	ret = descend( arcp -> arc_childp , stkstart , stkp + 1 );
	arcp -> arc_childp -> flags &= ~VISITED;
	if ( ret == FALSE )
	    return( FALSE );
    }
    return( TRUE );
}

bool
addcycle(arctype **stkstart, arctype **stkend)
{
    arctype	**arcpp;
    arctype	**stkloc;
    arctype	**stkp;
    arctype	**endlist;
    arctype	*minarc;
    arctype	*arcp;
    cltype	*clp;
    int		size;

    size = stkend - stkstart + 1;
    if ( size <= 1 )
	return( TRUE );
    for ( arcpp = stkstart , minarc = *arcpp ; arcpp <= stkend ; arcpp++ ) {
	if ( *arcpp > minarc )
	    continue;
	minarc = *arcpp;
	stkloc = arcpp;
    }
    for ( clp = cyclehead ; clp ; clp = clp -> next ) {
	if ( clp -> size != size )
	    continue;
	stkp = stkloc;
	endlist = &clp -> list[ size ];
	for ( arcpp = clp -> list ; arcpp < endlist ; arcpp++ ) {
	    if ( *stkp++ != *arcpp )
		break;
	    if ( stkp > stkend )
		stkp = stkstart;
	}
	if ( arcpp == endlist ) {
#	    ifdef DEBUG
		oldcycle++;
#	    endif /* DEBUG */
	    return( TRUE );
	}
    }
    clp = (cltype *)
	calloc( 1 , sizeof ( cltype ) + ( size - 1 ) * sizeof( arctype * ) );
    if ( clp == NULL ) {
	warnx( "no room for %zu bytes of subcycle storage" ,
	    sizeof ( cltype ) + ( size - 1 ) * sizeof( arctype * ) );
	return( FALSE );
    }
    stkp = stkloc;
    endlist = &clp -> list[ size ];
    for ( arcpp = clp -> list ; arcpp < endlist ; arcpp++ ) {
	arcp = *arcpp = *stkp++;
	if ( stkp > stkend )
	    stkp = stkstart;
	arcp -> arc_cyclecnt++;
	if ( ( arcp -> arc_flags & ONLIST ) == 0 ) {
	    arcp -> arc_flags |= ONLIST;
	    arcp -> arc_next = archead;
	    archead = arcp;
	}
    }
    clp -> size = size;
    clp -> next = cyclehead;
    cyclehead = clp;
#   ifdef DEBUG
	newcycle++;
	if ( debug & SUBCYCLELIST ) {
	    printsubcycle( clp );
	}
#   endif /* DEBUG */
    cyclecnt++;
    if ( cyclecnt >= CYCLEMAX )
	return( FALSE );
    return( TRUE );
}

void
compresslist(void)
{
    cltype	*clp;
    cltype	**prev;
    arctype	**arcpp;
    arctype	**endlist;
    arctype	*arcp;
    arctype	*maxarcp;
    arctype	*maxexitarcp;
    arctype	*maxwithparentarcp;
    arctype	*maxnoparentarcp;
    int		maxexitcnt;
    int		maxwithparentcnt;
    int		maxnoparentcnt;
#   ifdef DEBUG
	const char	*type;
#   endif /* DEBUG */

    maxexitcnt = 0;
    maxwithparentcnt = 0;
    maxnoparentcnt = 0;
    for ( endlist = &archead , arcp = archead ; arcp ; ) {
	if ( arcp -> arc_cyclecnt == 0 ) {
	    arcp -> arc_flags &= ~ONLIST;
	    *endlist = arcp -> arc_next;
	    arcp -> arc_next = 0;
	    arcp = *endlist;
	    continue;
	}
	if ( arcp -> arc_childp -> flags & HASCYCLEXIT ) {
	    if ( arcp -> arc_cyclecnt > maxexitcnt ||
		( arcp -> arc_cyclecnt == maxexitcnt &&
		arcp -> arc_cyclecnt < maxexitarcp -> arc_count ) ) {
		maxexitcnt = arcp -> arc_cyclecnt;
		maxexitarcp = arcp;
	    }
	} else if ( arcp -> arc_childp -> parentcnt > 1 ) {
	    if ( arcp -> arc_cyclecnt > maxwithparentcnt ||
		( arcp -> arc_cyclecnt == maxwithparentcnt &&
		arcp -> arc_cyclecnt < maxwithparentarcp -> arc_count ) ) {
		maxwithparentcnt = arcp -> arc_cyclecnt;
		maxwithparentarcp = arcp;
	    }
	} else {
	    if ( arcp -> arc_cyclecnt > maxnoparentcnt ||
		( arcp -> arc_cyclecnt == maxnoparentcnt &&
		arcp -> arc_cyclecnt < maxnoparentarcp -> arc_count ) ) {
		maxnoparentcnt = arcp -> arc_cyclecnt;
		maxnoparentarcp = arcp;
	    }
	}
	endlist = &arcp -> arc_next;
	arcp = arcp -> arc_next;
    }
    if ( maxexitcnt > 0 ) {
	/*
	 *	first choice is edge leading to node with out-of-cycle parent
	 */
	maxarcp = maxexitarcp;
#	ifdef DEBUG
	    type = "exit";
#	endif /* DEBUG */
    } else if ( maxwithparentcnt > 0 ) {
	/*
	 *	second choice is edge leading to node with at least one
	 *	other in-cycle parent
	 */
	maxarcp = maxwithparentarcp;
#	ifdef DEBUG
	    type = "internal";
#	endif /* DEBUG */
    } else {
	/*
	 *	last choice is edge leading to node with only this arc as
	 *	a parent (as it will now be orphaned)
	 */
	maxarcp = maxnoparentarcp;
#	ifdef DEBUG
	    type = "orphan";
#	endif /* DEBUG */
    }
    maxarcp -> arc_flags |= DEADARC;
    maxarcp -> arc_childp -> parentcnt -= 1;
    maxarcp -> arc_childp -> npropcall -= maxarcp -> arc_count;
#   ifdef DEBUG
	if ( debug & BREAKCYCLE ) {
	    printf( "%s delete %s arc: %s (%ld) -> %s from %u cycle(s)\n" ,
		"[compresslist]" , type , maxarcp -> arc_parentp -> name ,
		maxarcp -> arc_count , maxarcp -> arc_childp -> name ,
		maxarcp -> arc_cyclecnt );
	}
#   endif /* DEBUG */
    printf( "\t%s to %s with %ld calls\n" , maxarcp -> arc_parentp -> name ,
	maxarcp -> arc_childp -> name , maxarcp -> arc_count );
    prev = &cyclehead;
    for ( clp = cyclehead ; clp ; ) {
	endlist = &clp -> list[ clp -> size ];
	for ( arcpp = clp -> list ; arcpp < endlist ; arcpp++ )
	    if ( (*arcpp) -> arc_flags & DEADARC )
		break;
	if ( arcpp == endlist ) {
	    prev = &clp -> next;
	    clp = clp -> next;
	    continue;
	}
	for ( arcpp = clp -> list ; arcpp < endlist ; arcpp++ )
	    (*arcpp) -> arc_cyclecnt--;
	cyclecnt--;
	*prev = clp -> next;
	clp = clp -> next;
	free( clp );
    }
}

#ifdef DEBUG
void
printsubcycle(cltype *clp)
{
    arctype	**arcpp;
    arctype	**endlist;

    arcpp = clp -> list;
    printf( "%s <cycle %d>\n" , (*arcpp) -> arc_parentp -> name ,
	(*arcpp) -> arc_parentp -> cycleno ) ;
    for ( endlist = &clp -> list[ clp -> size ]; arcpp < endlist ; arcpp++ )
	printf( "\t(%ld) -> %s\n" , (*arcpp) -> arc_count ,
	    (*arcpp) -> arc_childp -> name ) ;
}
#endif /* DEBUG */

void
cycletime(void)
{
    int			cycle;
    nltype		*cyclenlp;
    nltype		*childp;

    for ( cycle = 1 ; cycle <= ncycle ; cycle += 1 ) {
	cyclenlp = &cyclenl[ cycle ];
	for ( childp = cyclenlp -> cnext ; childp ; childp = childp -> cnext ) {
	    if ( childp -> propfraction == 0.0 ) {
		    /*
		     * all members have the same propfraction except those
		     *	that were excluded with -E
		     */
		continue;
	    }
	    cyclenlp -> time += childp -> time;
	}
	cyclenlp -> propself = cyclenlp -> propfraction * cyclenlp -> time;
    }
}

    /*
     *	in one top to bottom pass over the topologically sorted namelist
     *	propagate:
     *		printflag as the union of parents' printflags
     *		propfraction as the sum of fractional parents' propfractions
     *	and while we're here, sum time for functions.
     */
void
doflags(void)
{
    int		index;
    nltype	*childp;
    nltype	*oldhead;

    oldhead = 0;
    for ( index = nname-1 ; index >= 0 ; index -= 1 ) {
	childp = topsortnlp[ index ];
	    /*
	     *	if we haven't done this function or cycle,
	     *	inherit things from parent.
	     *	this way, we are linear in the number of arcs
	     *	since we do all members of a cycle (and the cycle itself)
	     *	as we hit the first member of the cycle.
	     */
	if ( childp -> cyclehead != oldhead ) {
	    oldhead = childp -> cyclehead;
	    inheritflags( childp );
	}
#	ifdef DEBUG
	    if ( debug & PROPDEBUG ) {
		printf( "[doflags] " );
		printname( childp );
		printf( " inherits printflag %d and propfraction %f\n" ,
			childp -> printflag , childp -> propfraction );
	    }
#	endif /* DEBUG */
	if ( ! childp -> printflag ) {
		/*
		 *	printflag is off
		 *	it gets turned on by
		 *	being on -f list,
		 *	or there not being any -f list and not being on -e list.
		 */
	    if (   onlist( flist , childp -> name )
		|| ( !fflag && !onlist( elist , childp -> name ) ) ) {
		childp -> printflag = TRUE;
	    }
	} else {
		/*
		 *	this function has printing parents:
		 *	maybe someone wants to shut it up
		 *	by putting it on -e list.  (but favor -f over -e)
		 */
	    if (  ( !onlist( flist , childp -> name ) )
		&& onlist( elist , childp -> name ) ) {
		childp -> printflag = FALSE;
	    }
	}
	if ( childp -> propfraction == 0.0 ) {
		/*
		 *	no parents to pass time to.
		 *	collect time from children if
		 *	its on -F list,
		 *	or there isn't any -F list and its not on -E list.
		 */
	    if ( onlist( Flist , childp -> name )
		|| ( !Fflag && !onlist( Elist , childp -> name ) ) ) {
		    childp -> propfraction = 1.0;
	    }
	} else {
		/*
		 *	it has parents to pass time to,
		 *	but maybe someone wants to shut it up
		 *	by putting it on -E list.  (but favor -F over -E)
		 */
	    if (  !onlist( Flist , childp -> name )
		&& onlist( Elist , childp -> name ) ) {
		childp -> propfraction = 0.0;
	    }
	}
	childp -> propself = childp -> time * childp -> propfraction;
	printtime += childp -> propself;
#	ifdef DEBUG
	    if ( debug & PROPDEBUG ) {
		printf( "[doflags] " );
		printname( childp );
		printf( " ends up with printflag %d and propfraction %f\n" ,
			childp -> printflag , childp -> propfraction );
		printf( "time %f propself %f printtime %f\n" ,
			childp -> time , childp -> propself , printtime );
	    }
#	endif /* DEBUG */
    }
}

    /*
     *	check if any parent of this child
     *	(or outside parents of this cycle)
     *	have their print flags on and set the
     *	print flag of the child (cycle) appropriately.
     *	similarly, deal with propagation fractions from parents.
     */
void
inheritflags(nltype *childp)
{
    nltype	*headp;
    arctype	*arcp;
    nltype	*parentp;
    nltype	*memp;

    headp = childp -> cyclehead;
    if ( childp == headp ) {
	    /*
	     *	just a regular child, check its parents
	     */
	childp -> printflag = FALSE;
	childp -> propfraction = 0.0;
	for (arcp = childp -> parents ; arcp ; arcp = arcp -> arc_parentlist) {
	    parentp = arcp -> arc_parentp;
	    if ( childp == parentp ) {
		continue;
	    }
	    childp -> printflag |= parentp -> printflag;
		/*
		 *	if the child was never actually called
		 *	(e.g. this arc is static (and all others are, too))
		 *	no time propagates along this arc.
		 */
	    if ( arcp -> arc_flags & DEADARC ) {
		continue;
	    }
	    if ( childp -> npropcall ) {
		childp -> propfraction += parentp -> propfraction
					* ( ( (double) arcp -> arc_count )
					  / ( (double) childp -> npropcall ) );
	    }
	}
    } else {
	    /*
	     *	its a member of a cycle, look at all parents from
	     *	outside the cycle
	     */
	headp -> printflag = FALSE;
	headp -> propfraction = 0.0;
	for ( memp = headp -> cnext ; memp ; memp = memp -> cnext ) {
	    for (arcp = memp->parents ; arcp ; arcp = arcp->arc_parentlist) {
		if ( arcp -> arc_parentp -> cyclehead == headp ) {
		    continue;
		}
		parentp = arcp -> arc_parentp;
		headp -> printflag |= parentp -> printflag;
		    /*
		     *	if the cycle was never actually called
		     *	(e.g. this arc is static (and all others are, too))
		     *	no time propagates along this arc.
		     */
		if ( arcp -> arc_flags & DEADARC ) {
		    continue;
		}
		if ( headp -> npropcall ) {
		    headp -> propfraction += parentp -> propfraction
					* ( ( (double) arcp -> arc_count )
					  / ( (double) headp -> npropcall ) );
		}
	    }
	}
	for ( memp = headp ; memp ; memp = memp -> cnext ) {
	    memp -> printflag = headp -> printflag;
	    memp -> propfraction = headp -> propfraction;
	}
    }
}
