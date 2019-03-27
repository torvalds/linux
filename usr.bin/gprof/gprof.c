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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1983, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#if 0
#ifndef lint
static char sccsid[] = "@(#)gprof.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "gprof.h"

static int valcmp(const void *, const void *);

static struct gmonhdr	gmonhdr;
static int lflag;
static int Lflag;

int
main(int argc, char **argv)
{
    char	**sp;
    nltype	**timesortnlp;
    char	**defaultEs;

    --argc;
    argv++;
    debug = 0;
    bflag = TRUE;
    while ( *argv != 0 && **argv == '-' ) {
	(*argv)++;
	switch ( **argv ) {
	case 'a':
	    aflag = TRUE;
	    break;
	case 'b':
	    bflag = FALSE;
	    break;
	case 'C':
	    Cflag = TRUE;
	    cyclethreshold = atoi( *++argv );
	    break;
	case 'd':
	    dflag = TRUE;
	    setlinebuf(stdout);
	    debug |= atoi( *++argv );
	    debug |= ANYDEBUG;
#	    ifdef DEBUG
		printf("[main] debug = %d\n", debug);
#	    else /* not DEBUG */
		printf("gprof: -d ignored\n");
#	    endif /* DEBUG */
	    break;
	case 'E':
	    ++argv;
	    addlist( Elist , *argv );
	    Eflag = TRUE;
	    addlist( elist , *argv );
	    eflag = TRUE;
	    break;
	case 'e':
	    addlist( elist , *++argv );
	    eflag = TRUE;
	    break;
	case 'F':
	    ++argv;
	    addlist( Flist , *argv );
	    Fflag = TRUE;
	    addlist( flist , *argv );
	    fflag = TRUE;
	    break;
	case 'f':
	    addlist( flist , *++argv );
	    fflag = TRUE;
	    break;
	case 'k':
	    addlist( kfromlist , *++argv );
	    addlist( ktolist , *++argv );
	    kflag = TRUE;
	    break;
	case 'K':
	    Kflag = TRUE;
	    break;
	case 'l':
	    lflag = 1;
	    Lflag = 0;
	    break;
	case 'L':
	    Lflag = 1;
	    lflag = 0;
	    break;
	case 's':
	    sflag = TRUE;
	    break;
	case 'u':
	    uflag = TRUE;
	    break;
	case 'z':
	    zflag = TRUE;
	    break;
	}
	argv++;
    }
    if ( *argv != 0 ) {
	a_outname  = *argv;
	argv++;
    } else {
	a_outname  = A_OUTNAME;
    }
    if ( *argv != 0 ) {
	gmonname = *argv;
	argv++;
    } else {
	gmonname = (char *) malloc(strlen(a_outname)+6);
	strcpy(gmonname, a_outname);
	strcat(gmonname, ".gmon");
    }
	/*
	 *	get information from the executable file.
	 */
    if ((Kflag && kernel_getnfile(a_outname, &defaultEs) == -1) ||
      (!Kflag && elf_getnfile(a_outname, &defaultEs) == -1 &&
      aout_getnfile(a_outname, &defaultEs) == -1))
	errx(1, "%s: bad format", a_outname);
	/*
	 *	sort symbol table.
	 */
    qsort(nl, nname, sizeof(nltype), valcmp);
	/*
	 *	turn off default functions
	 */
    for ( sp = defaultEs ; *sp ; sp++ ) {
	Eflag = TRUE;
	addlist( Elist , *sp );
	eflag = TRUE;
	addlist( elist , *sp );
    }
	/*
	 *	get information about mon.out file(s).
	 */
    do	{
	getpfile( gmonname );
	if ( *argv != 0 ) {
	    gmonname = *argv;
	}
    } while ( *argv++ != 0 );
	/*
	 *	how many ticks per second?
	 *	if we can't tell, report time in ticks.
	 */
    if (hz == 0) {
	hz = 1;
	fprintf(stderr, "time is in ticks, not seconds\n");
    }
	/*
	 *	dump out a gmon.sum file if requested
	 */
    if ( sflag ) {
	dumpsum( GMONSUM );
    }
	/*
	 *	assign samples to procedures
	 */
    asgnsamples();
	/*
	 *	assemble the dynamic profile
	 */
    timesortnlp = doarcs();
	/*
	 *	print the dynamic profile
	 */
    if(!lflag) {
	    printgprof( timesortnlp );
    }
	/*
	 *	print the flat profile
	 */
    if(!Lflag) {
	    printprof();
    }
	/*
	 *	print the index
	 */
    printindex();
    exit(0);
}

    /*
     *	information from a gmon.out file is in two parts:
     *	an array of sampling hits within pc ranges,
     *	and the arcs.
     */
void
getpfile(char *filename)
{
    FILE		*pfile;
    struct rawarc	arc;

    pfile = openpfile(filename);
    readsamples(pfile);
	/*
	 *	the rest of the file consists of
	 *	a bunch of <from,self,count> tuples.
	 */
    while ( fread( &arc , sizeof arc , 1 , pfile ) == 1 ) {
#	ifdef DEBUG
	    if ( debug & SAMPLEDEBUG ) {
		printf( "[getpfile] frompc 0x%lx selfpc 0x%lx count %ld\n" ,
			arc.raw_frompc , arc.raw_selfpc , arc.raw_count );
	    }
#	endif /* DEBUG */
	    /*
	     *	add this arc
	     */
	tally( &arc );
    }
    fclose(pfile);
}

FILE *
openpfile(char *filename)
{
    struct gmonhdr	tmp;
    FILE		*pfile;
    int			size;
    int			rate;

    if((pfile = fopen(filename, "r")) == NULL)
	err(1, "%s", filename);
    fread(&tmp, sizeof(struct gmonhdr), 1, pfile);
    if ( s_highpc != 0 && ( tmp.lpc != gmonhdr.lpc ||
	 tmp.hpc != gmonhdr.hpc || tmp.ncnt != gmonhdr.ncnt ) )
	errx(1, "%s: incompatible with first gmon file", filename);
    gmonhdr = tmp;
    if ( gmonhdr.version == GMONVERSION ) {
	rate = gmonhdr.profrate;
	size = sizeof(struct gmonhdr);
    } else {
	fseek(pfile, sizeof(struct ophdr), SEEK_SET);
	size = sizeof(struct ophdr);
	gmonhdr.profrate = rate = hertz();
	gmonhdr.version = GMONVERSION;
    }
    if (hz == 0) {
	hz = rate;
    } else if (hz != rate)
	errx(0, "%s: profile clock rate (%d) %s (%ld) in first gmon file",
	    filename, rate, "incompatible with clock rate", hz);
    if ( gmonhdr.histcounter_type == 0 ) {
	/* Historical case.  The type was u_short (2 bytes in practice). */
	histcounter_type = 16;
	histcounter_size = 2;
    } else {
	histcounter_type = gmonhdr.histcounter_type;
	histcounter_size = abs(histcounter_type) / CHAR_BIT;
    }
    s_lowpc = (unsigned long) gmonhdr.lpc;
    s_highpc = (unsigned long) gmonhdr.hpc;
    lowpc = (unsigned long)gmonhdr.lpc / HISTORICAL_SCALE_2;
    highpc = (unsigned long)gmonhdr.hpc / HISTORICAL_SCALE_2;
    sampbytes = gmonhdr.ncnt - size;
    nsamples = sampbytes / histcounter_size;
#   ifdef DEBUG
	if ( debug & SAMPLEDEBUG ) {
	    printf( "[openpfile] hdr.lpc 0x%lx hdr.hpc 0x%lx hdr.ncnt %d\n",
		gmonhdr.lpc , gmonhdr.hpc , gmonhdr.ncnt );
	    printf( "[openpfile]   s_lowpc 0x%lx   s_highpc 0x%lx\n" ,
		s_lowpc , s_highpc );
	    printf( "[openpfile]     lowpc 0x%lx     highpc 0x%lx\n" ,
		lowpc , highpc );
	    printf( "[openpfile] sampbytes %d nsamples %d\n" ,
		sampbytes , nsamples );
	    printf( "[openpfile] sample rate %ld\n" , hz );
	}
#   endif /* DEBUG */
    return(pfile);
}

void
tally(struct rawarc *rawp)
{
    nltype		*parentp;
    nltype		*childp;

    parentp = nllookup( rawp -> raw_frompc );
    childp = nllookup( rawp -> raw_selfpc );
    if ( parentp == 0 || childp == 0 )
	return;
    if ( kflag
	 && onlist( kfromlist , parentp -> name )
	 && onlist( ktolist , childp -> name ) ) {
	return;
    }
    childp -> ncall += rawp -> raw_count;
#   ifdef DEBUG
	if ( debug & TALLYDEBUG ) {
	    printf( "[tally] arc from %s to %s traversed %ld times\n" ,
		    parentp -> name , childp -> name , rawp -> raw_count );
	}
#   endif /* DEBUG */
    addarc( parentp , childp , rawp -> raw_count );
}

/*
 * dump out the gmon.sum file
 */
void
dumpsum(const char *sumfile)
{
    register nltype *nlp;
    register arctype *arcp;
    struct rawarc arc;
    FILE *sfile;

    if ( ( sfile = fopen ( sumfile , "w" ) ) == NULL )
	err( 1 , "%s" , sumfile );
    /*
     * dump the header; use the last header read in
     */
    if ( fwrite( &gmonhdr , sizeof gmonhdr , 1 , sfile ) != 1 )
	err( 1 , "%s" , sumfile );
    /*
     * dump the samples
     */
    if (fwrite(samples, histcounter_size, nsamples, sfile) != nsamples)
	err( 1 , "%s" , sumfile );
    /*
     * dump the normalized raw arc information
     */
    for ( nlp = nl ; nlp < npe ; nlp++ ) {
	for ( arcp = nlp -> children ; arcp ; arcp = arcp -> arc_childlist ) {
	    arc.raw_frompc = arcp -> arc_parentp -> value;
	    arc.raw_selfpc = arcp -> arc_childp -> value;
	    arc.raw_count = arcp -> arc_count;
	    if ( fwrite ( &arc , sizeof arc , 1 , sfile ) != 1 )
		err( 1 , "%s" , sumfile );
#	    ifdef DEBUG
		if ( debug & SAMPLEDEBUG ) {
		    printf( "[dumpsum] frompc 0x%lx selfpc 0x%lx count %ld\n" ,
			    arc.raw_frompc , arc.raw_selfpc , arc.raw_count );
		}
#	    endif /* DEBUG */
	}
    }
    fclose( sfile );
}

static int
valcmp(const void *v1, const void *v2)
{
    const nltype *p1 = (const nltype *)v1;
    const nltype *p2 = (const nltype *)v2;

    if ( p1 -> value < p2 -> value ) {
	return LESSTHAN;
    }
    if ( p1 -> value > p2 -> value ) {
	return GREATERTHAN;
    }
    return EQUALTO;
}

void
readsamples(FILE *pfile)
{
    int		i;
    intmax_t	sample;

    if (samples == 0) {
	samples = (double *) calloc(nsamples, sizeof(double));
	if (samples == NULL)
	    errx(0, "no room for %d sample pc's", nsamples);
    }
    for (i = 0; i < nsamples; i++) {
	fread(&sample, histcounter_size, 1, pfile);
	if (feof(pfile))
		break;
	switch ( histcounter_type ) {
	case -8:
	    samples[i] += *(int8_t *)&sample;
	    break;
	case 8:
	    samples[i] += *(u_int8_t *)&sample;
	    break;
	case -16:
	    samples[i] += *(int16_t *)&sample;
	    break;
	case 16:
	    samples[i] += *(u_int16_t *)&sample;
	    break;
	case -32:
	    samples[i] += *(int32_t *)&sample;
	    break;
	case 32:
	    samples[i] += *(u_int32_t *)&sample;
	    break;
	case -64:
	    samples[i] += *(int64_t *)&sample;
	    break;
	case 64:
	    samples[i] += *(u_int64_t *)&sample;
	    break;
	default:
	    err(1, "unsupported histogram counter type %d", histcounter_type);
	}
    }
    if (i != nsamples)
	errx(1, "unexpected EOF after reading %d/%d samples", --i , nsamples );
}

/*
 *	Assign samples to the procedures to which they belong.
 *
 *	There are three cases as to where pcl and pch can be
 *	with respect to the routine entry addresses svalue0 and svalue1
 *	as shown in the following diagram.  overlap computes the
 *	distance between the arrows, the fraction of the sample
 *	that is to be credited to the routine which starts at svalue0.
 *
 *	    svalue0                                         svalue1
 *	       |                                               |
 *	       v                                               v
 *
 *	       +-----------------------------------------------+
 *	       |					       |
 *	  |  ->|    |<-		->|         |<-		->|    |<-  |
 *	  |         |		  |         |		  |         |
 *	  +---------+		  +---------+		  +---------+
 *
 *	  ^         ^		  ^         ^		  ^         ^
 *	  |         |		  |         |		  |         |
 *	 pcl       pch		 pcl       pch		 pcl       pch
 *
 *	For the vax we assert that samples will never fall in the first
 *	two bytes of any routine, since that is the entry mask,
 *	thus we give call alignentries() to adjust the entry points if
 *	the entry mask falls in one bucket but the code for the routine
 *	doesn't start until the next bucket.  In conjunction with the
 *	alignment of routine addresses, this should allow us to have
 *	only one sample for every four bytes of text space and never
 *	have any overlap (the two end cases, above).
 */
void
asgnsamples(void)
{
    register int	j;
    double		ccnt;
    double		thetime;
    unsigned long	pcl, pch;
    register int	i;
    unsigned long	overlap;
    unsigned long	svalue0, svalue1;

    /* read samples and assign to namelist symbols */
    scale = highpc - lowpc;
    scale /= nsamples;
    alignentries();
    for (i = 0, j = 1; i < nsamples; i++) {
	ccnt = samples[i];
	if (ccnt == 0)
		continue;
	pcl = lowpc + (unsigned long)(scale * i);
	pch = lowpc + (unsigned long)(scale * (i + 1));
	thetime = ccnt;
#	ifdef DEBUG
	    if ( debug & SAMPLEDEBUG ) {
		printf( "[asgnsamples] pcl 0x%lx pch 0x%lx ccnt %.0f\n" ,
			pcl , pch , ccnt );
	    }
#	endif /* DEBUG */
	totime += thetime;
	for (j = j - 1; j < nname; j++) {
	    svalue0 = nl[j].svalue;
	    svalue1 = nl[j+1].svalue;
		/*
		 *	if high end of tick is below entry address,
		 *	go for next tick.
		 */
	    if (pch < svalue0)
		    break;
		/*
		 *	if low end of tick into next routine,
		 *	go for next routine.
		 */
	    if (pcl >= svalue1)
		    continue;
	    overlap = min(pch, svalue1) - max(pcl, svalue0);
	    if (overlap > 0) {
#		ifdef DEBUG
		    if (debug & SAMPLEDEBUG) {
			printf("[asgnsamples] (0x%lx->0x%lx-0x%lx) %s gets %f ticks %lu overlap\n",
				nl[j].value / HISTORICAL_SCALE_2,
				svalue0, svalue1, nl[j].name,
				overlap * thetime / scale, overlap);
		    }
#		endif /* DEBUG */
		nl[j].time += overlap * thetime / scale;
	    }
	}
    }
#   ifdef DEBUG
	if (debug & SAMPLEDEBUG) {
	    printf("[asgnsamples] totime %f\n", totime);
	}
#   endif /* DEBUG */
}


unsigned long
min(unsigned long a, unsigned long b)
{
    if (a<b)
	return(a);
    return(b);
}

unsigned long
max(unsigned long a, unsigned long b)
{
    if (a>b)
	return(a);
    return(b);
}

    /*
     *	calculate scaled entry point addresses (to save time in asgnsamples),
     *	and possibly push the scaled entry points over the entry mask,
     *	if it turns out that the entry point is in one bucket and the code
     *	for a routine is in the next bucket.
     */
void
alignentries(void)
{
    register struct nl	*nlp;
    unsigned long	bucket_of_entry;
    unsigned long	bucket_of_code;

    for (nlp = nl; nlp < npe; nlp++) {
	nlp -> svalue = nlp -> value / HISTORICAL_SCALE_2;
	bucket_of_entry = (nlp->svalue - lowpc) / scale;
	bucket_of_code = (nlp->svalue + OFFSET_OF_CODE / HISTORICAL_SCALE_2 -
	  lowpc) / scale;
	if (bucket_of_entry < bucket_of_code) {
#	    ifdef DEBUG
		if (debug & SAMPLEDEBUG) {
		    printf("[alignentries] pushing svalue 0x%lx to 0x%lx\n",
			    nlp->svalue,
			    nlp->svalue + OFFSET_OF_CODE / HISTORICAL_SCALE_2);
		}
#	    endif /* DEBUG */
	    nlp->svalue += OFFSET_OF_CODE / HISTORICAL_SCALE_2;
	}
    }
}
