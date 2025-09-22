/*	$OpenBSD: gettytab.h,v 1.9 2024/05/19 10:30:43 jsg Exp $*/

/*
 * Copyright (c) 1983, 1993, 1994
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
 *
 *	from: @(#)gettytab.h	8.2 (Berkeley) 3/30/94
 */

/*
 * Getty description definitions.
 */
struct	gettystrs {
	char	*field;		/* name to lookup in gettytab */
	char	*defalt;	/* value we find by looking in defaults */
	char	*value;		/* value that we find there */
};

struct	gettynums {
	char	*field;		/* name to lookup */
	long	defalt;		/* number we find in defaults */
	long	value;		/* number we find there */
	int	set;		/* we actually got this one */
};

struct gettyflags {
	char	*field;		/* name to lookup */
	char	invrt;		/* name existing in gettytab --> false */
	char	defalt;		/* true/false in defaults */
	char	value;		/* true/false flag */
	char	set;		/* we found it */
};

/*
 * String values.
 */
#define	NX	gettystrs[0].value
#define	CL	gettystrs[1].value
#define IM	gettystrs[2].value
#define	LM	gettystrs[3].value
#define	ER	gettystrs[4].value
#define	KL	gettystrs[5].value
#define	ET	gettystrs[6].value
#define	PC	gettystrs[7].value
#define	TT	gettystrs[8].value
#define	EV	gettystrs[9].value
#define	LO	gettystrs[10].value
#define HN	gettystrs[11].value
#define HE	gettystrs[12].value
#define IN	gettystrs[13].value
#define QU	gettystrs[14].value
#define XN	gettystrs[15].value
#define XF	gettystrs[16].value
#define BK	gettystrs[17].value
#define SU	gettystrs[18].value
#define DS	gettystrs[19].value
#define RP	gettystrs[20].value
#define FL	gettystrs[21].value
#define WE	gettystrs[22].value
#define LN	gettystrs[23].value

/*
 * Numeric definitions.
 */
#define	IS	gettynums[0].value
#define	OS	gettynums[1].value
#define	SP	gettynums[2].value
#define	ND	gettynums[3].value
#define	CD	gettynums[4].value
#define	TD	gettynums[5].value
#define	FD	gettynums[6].value
#define	BD	gettynums[7].value
#define	TO	gettynums[8].value
#define	PF	gettynums[9].value
#define	C0	gettynums[10].value
#define	C0set	gettynums[10].set
#define	C1	gettynums[11].value
#define	C1set	gettynums[11].set
#define	C2	gettynums[12].value
#define	C2set	gettynums[12].set
#define	I0	gettynums[13].value
#define	I0set	gettynums[13].set
#define	I1	gettynums[14].value
#define	I1set	gettynums[14].set
#define	I2	gettynums[15].value
#define	I2set	gettynums[15].set
#define	L0	gettynums[16].value
#define	L0set	gettynums[16].set
#define	L1	gettynums[17].value
#define	L1set	gettynums[17].set
#define	L2	gettynums[18].value
#define	L2set	gettynums[18].set
#define	O0	gettynums[19].value
#define	O0set	gettynums[19].set
#define	O1	gettynums[20].value
#define	O1set	gettynums[20].set
#define	O2	gettynums[21].value
#define	O2set	gettynums[21].set

/*
 * Boolean values.
 */
#define	HT	gettyflags[0].value
#define	NL	gettyflags[1].value
#define	EP	gettyflags[2].value
#define	EPset	gettyflags[2].set
#define	OP	gettyflags[3].value
#define	OPset	gettyflags[3].set
#define	AP	gettyflags[4].value
#define	APset	gettyflags[4].set
#define	EC	gettyflags[5].value
#define	CO	gettyflags[6].value
#define	CB	gettyflags[7].value
#define	CK	gettyflags[8].value
#define	CE	gettyflags[9].value
#define	PE	gettyflags[10].value
#define	RW	gettyflags[11].value
#define	XC	gettyflags[12].value
#define	LC	gettyflags[13].value
#define	UC	gettyflags[14].value
#define	IG	gettyflags[15].value
#define	PS	gettyflags[16].value
#define	HC	gettyflags[17].value
#define UB	gettyflags[18].value
#define AB	gettyflags[19].value
#define DX	gettyflags[20].value
#define	NP	gettyflags[21].value
#define	MB	gettyflags[22].value

extern	struct gettyflags gettyflags[];
extern	struct gettynums gettynums[];
extern	struct gettystrs gettystrs[];
