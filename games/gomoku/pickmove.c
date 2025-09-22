/*	$OpenBSD: pickmove.c,v 1.16 2016/01/08 21:38:33 mestre Exp $	*/
/*
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
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

#include <curses.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "gomoku.h"

#define BITS_PER_INT	(sizeof(int) * CHAR_BIT)
#define MAPSZ		(BAREA / BITS_PER_INT)

#define BIT_SET(a, b)	((a)[(b)/BITS_PER_INT] |= (1 << ((b) % BITS_PER_INT)))
#define BIT_CLR(a, b)	((a)[(b)/BITS_PER_INT] &= ~(1 << ((b) % BITS_PER_INT)))
#define BIT_TEST(a, b)	((a)[(b)/BITS_PER_INT] & (1 << ((b) % BITS_PER_INT)))

struct	combostr *hashcombos[FAREA];	/* hash list for finding duplicates */
struct	combostr *sortcombos;		/* combos at higher levels */
int	combolen;			/* number of combos in sortcombos */
int	nextcolor;			/* color of next move */
int	elistcnt;			/* count of struct elist allocated */
int	combocnt;			/* count of struct combostr allocated */
int	forcemap[MAPSZ];		/* map for blocking <1,x> combos */
int	tmpmap[MAPSZ];			/* map for blocking <1,x> combos */
int	nforce;				/* count of opponent <1,x> combos */

int
pickmove(int us)
{
	struct spotstr *sp, *sp1, *sp2;
	union comboval *Ocp, *Tcp;
	int m;

	/* first move is easy */
	if (movenum == 1)
		return (PT(K,10));

	/* initialize all the board values */
	for (sp = &board[PT(T,20)]; --sp >= &board[PT(A,1)]; ) {
		sp->s_combo[BLACK].s = MAXCOMBO + 1;
		sp->s_combo[WHITE].s = MAXCOMBO + 1;
		sp->s_level[BLACK] = 255;
		sp->s_level[WHITE] = 255;
		sp->s_nforce[BLACK] = 0;
		sp->s_nforce[WHITE] = 0;
		sp->s_flg &= ~(FFLAGALL | MFLAGALL);
	}
	nforce = 0;
	memset(forcemap, 0, sizeof(forcemap));

	/* compute new values */
	nextcolor = us;
	scanframes(BLACK);
	scanframes(WHITE);

	/* find the spot with the highest value */
	for (sp = sp1 = sp2 = &board[PT(T,19)]; --sp >= &board[PT(A,1)]; ) {
		if (sp->s_occ != EMPTY)
			continue;
		if (debug && (sp->s_combo[BLACK].c.a == 1 ||
		    sp->s_combo[WHITE].c.a == 1)) {
			snprintf(fmtbuf, sizeof fmtbuf,
				"- %s %x/%d %d %x/%d %d %d", stoc(sp - board),
				sp->s_combo[BLACK].s, sp->s_level[BLACK],
				sp->s_nforce[BLACK],
				sp->s_combo[WHITE].s, sp->s_level[WHITE],
				sp->s_nforce[WHITE],
				sp->s_wval);
			dlog(fmtbuf);
		}
		/* pick the best black move */
		if (better(sp, sp1, BLACK))
			sp1 = sp;
		/* pick the best white move */
		if (better(sp, sp2, WHITE))
			sp2 = sp;
	}

	if (debug) {
		snprintf(fmtbuf, sizeof fmtbuf,
			"B %s %x/%d %d %x/%d %d %d",
			stoc(sp1 - board),
			sp1->s_combo[BLACK].s, sp1->s_level[BLACK],
			sp1->s_nforce[BLACK],
			sp1->s_combo[WHITE].s, sp1->s_level[WHITE],
			sp1->s_nforce[WHITE], sp1->s_wval);
		dlog(fmtbuf);
		snprintf(fmtbuf, sizeof fmtbuf,
			"W %s %x/%d %d %x/%d %d %d",
			stoc(sp2 - board),
			sp2->s_combo[WHITE].s, sp2->s_level[WHITE],
			sp2->s_nforce[WHITE],
			sp2->s_combo[BLACK].s, sp2->s_level[BLACK],
			sp2->s_nforce[BLACK], sp2->s_wval);
		dlog(fmtbuf);
		/*
		 * Check for more than one force that can't
		 * all be blocked with one move.
		 */
		sp = (us == BLACK) ? sp2 : sp1;
		m = sp - board;
		if (sp->s_combo[!us].c.a == 1 && !BIT_TEST(forcemap, m))
			dlog("*** Can't be blocked");
	}
	if (us == BLACK) {
		Ocp = &sp1->s_combo[BLACK];
		Tcp = &sp2->s_combo[WHITE];
	} else {
		Tcp = &sp1->s_combo[BLACK];
		Ocp = &sp2->s_combo[WHITE];
		sp = sp1;
		sp1 = sp2;
		sp2 = sp;
	}
	/*
	 * Block their combo only if we have to (i.e., if they are one move
	 * away from completing a force and we don't have a force that
	 * we can complete which takes fewer moves to win).
	 */
	if (Tcp->c.a <= 1 && (Ocp->c.a > 1 ||
	    Tcp->c.a + Tcp->c.b < Ocp->c.a + Ocp->c.b))
		return (sp2 - board);
	return (sp1 - board);
}

/*
 * Return true if spot 'sp' is better than spot 'sp1' for color 'us'.
 */
int
better(struct spotstr *sp, struct spotstr *sp1, int us)
{
	int them, s, s1;

	if (sp->s_combo[us].s < sp1->s_combo[us].s)
		return (1);
	if (sp->s_combo[us].s != sp1->s_combo[us].s)
		return (0);
	if (sp->s_level[us] < sp1->s_level[us])
		return (1);
	if (sp->s_level[us] != sp1->s_level[us])
		return (0);
	if (sp->s_nforce[us] > sp1->s_nforce[us])
		return (1);
	if (sp->s_nforce[us] != sp1->s_nforce[us])
		return (0);

	them = !us;
	s = sp - board;
	s1 = sp1 - board;
	if (BIT_TEST(forcemap, s) && !BIT_TEST(forcemap, s1))
		return (1);
	if (!BIT_TEST(forcemap, s) && BIT_TEST(forcemap, s1))
		return (0);
	if (sp->s_combo[them].s < sp1->s_combo[them].s)
		return (1);
	if (sp->s_combo[them].s != sp1->s_combo[them].s)
		return (0);
	if (sp->s_level[them] < sp1->s_level[them])
		return (1);
	if (sp->s_level[them] != sp1->s_level[them])
		return (0);
	if (sp->s_nforce[them] > sp1->s_nforce[them])
		return (1);
	if (sp->s_nforce[them] != sp1->s_nforce[them])
		return (0);

	if (sp->s_wval > sp1->s_wval)
		return (1);
	if (sp->s_wval != sp1->s_wval)
		return (0);

	return (arc4random() & 1);
}

int	curcolor;	/* implicit parameter to makecombo() */
int	curlevel;	/* implicit parameter to makecombo() */

/*
 * Scan the sorted list of non-empty frames and
 * update the minimum combo values for each empty spot.
 * Also, try to combine frames to find more complex (chained) moves.
 */
void
scanframes(int color)
{
	struct combostr *cbp, *ecbp;
	struct spotstr *sp;
	union comboval *cp;
	struct elist *ep, *nep;
	int i, r, d, n;
	union comboval cb;

	curcolor = color;

	/* check for empty list of frames */
	cbp = sortframes[color];
	if (cbp == (struct combostr *)0)
		return;

	/* quick check for four in a row */
	sp = &board[cbp->c_vertex];
	cb.s = sp->s_fval[color][d = cbp->c_dir].s;
	if (cb.s < 0x101) {
		d = dd[d];
		for (i = 5 + cb.c.b; --i >= 0; sp += d) {
			if (sp->s_occ != EMPTY)
				continue;
			sp->s_combo[color].s = cb.s;
			sp->s_level[color] = 1;
		}
		return;
	}

	/*
	 * Update the minimum combo value for each spot in the frame
	 * and try making all combinations of two frames intersecting at
	 * an empty spot.
	 */
	n = combolen;
	ecbp = cbp;
	do {
		sp = &board[cbp->c_vertex];
		cp = &sp->s_fval[color][r = cbp->c_dir];
		d = dd[r];
		if (cp->c.b) {
			/*
			 * Since this is the first spot of an open ended
			 * frame, we treat it as a closed frame.
			 */
			cb.c.a = cp->c.a + 1;
			cb.c.b = 0;
			if (cb.s < sp->s_combo[color].s) {
				sp->s_combo[color].s = cb.s;
				sp->s_level[color] = 1;
			}
			/*
			 * Try combining other frames that intersect
			 * at this spot.
			 */
			makecombo2(cbp, sp, 0, cb.s);
			if (cp->s != 0x101)
				cb.s = cp->s;
			else if (color != nextcolor)
				memset(tmpmap, 0, sizeof(tmpmap));
			sp += d;
			i = 1;
		} else {
			cb.s = cp->s;
			i = 0;
		}
		for (; i < 5; i++, sp += d) {	/* for each spot */
			if (sp->s_occ != EMPTY)
				continue;
			if (cp->s < sp->s_combo[color].s) {
				sp->s_combo[color].s = cp->s;
				sp->s_level[color] = 1;
			}
			if (cp->s == 0x101) {
				sp->s_nforce[color]++;
				if (color != nextcolor) {
					n = sp - board;
					BIT_SET(tmpmap, n);
				}
			}
			/*
			 * Try combining other frames that intersect
			 * at this spot.
			 */
			makecombo2(cbp, sp, i, cb.s);
		}
		if (cp->s == 0x101 && color != nextcolor) {
			if (nforce == 0)
				memcpy(forcemap, tmpmap, sizeof(tmpmap));
			else {
				for (i = 0; i < MAPSZ; i++)
					forcemap[i] &= tmpmap[i];
			}
		}
		/* mark frame as having been processed */
		board[cbp->c_vertex].s_flg |= MFLAG << r;
	} while ((cbp = cbp->c_next) != ecbp);

	/*
	 * Try to make new 3rd level combos, 4th level, etc.
	 * Limit the search depth early in the game.
	 */
	d = 2;
	while (d <= ((unsigned)(movenum + 1) >> 1) && combolen > n) {
		if (debug) {
			snprintf(fmtbuf, sizeof fmtbuf,
				"%cL%d %d %d %d", "BW"[color],
				d, combolen - n, combocnt, elistcnt);
			dlog(fmtbuf);
			refresh();
		}
		n = combolen;
		addframes(d);
		d++;
	}

	/* scan for combos at empty spots */
	for (sp = &board[PT(T,20)]; --sp >= &board[PT(A,1)]; ) {
		for (ep = sp->s_empty; ep; ep = nep) {
			cbp = ep->e_combo;
			if (cbp->c_combo.s <= sp->s_combo[color].s) {
				if (cbp->c_combo.s != sp->s_combo[color].s) {
					sp->s_combo[color].s = cbp->c_combo.s;
					sp->s_level[color] = cbp->c_nframes;
				} else if (cbp->c_nframes < sp->s_level[color])
					sp->s_level[color] = cbp->c_nframes;
			}
			nep = ep->e_next;
			free(ep);
			elistcnt--;
		}
		sp->s_empty = (struct elist *)0;
		for (ep = sp->s_nempty; ep; ep = nep) {
			cbp = ep->e_combo;
			if (cbp->c_combo.s <= sp->s_combo[color].s) {
				if (cbp->c_combo.s != sp->s_combo[color].s) {
					sp->s_combo[color].s = cbp->c_combo.s;
					sp->s_level[color] = cbp->c_nframes;
				} else if (cbp->c_nframes < sp->s_level[color])
					sp->s_level[color] = cbp->c_nframes;
			}
			nep = ep->e_next;
			free(ep);
			elistcnt--;
		}
		sp->s_nempty = (struct elist *)0;
	}

	/* remove old combos */
	if ((cbp = sortcombos) != (struct combostr *)0) {
		struct combostr *ncbp;

		/* scan the list */
		ecbp = cbp;
		do {
			ncbp = cbp->c_next;
			free(cbp);
			combocnt--;
		} while ((cbp = ncbp) != ecbp);
		sortcombos = (struct combostr *)0;
	}
	combolen = 0;

#ifdef DEBUG
	if (combocnt) {
		snprintf(fmtbuf, sizeof fmtbuf,
			"scanframes: %c combocnt %d", "BW"[color],
			combocnt);
		dlog(fmtbuf);
		whatsup(0);
	}
	if (elistcnt) {
		snprintf(fmtbuf, sizeof fmtbuf,
			"scanframes: %c elistcnt %d", "BW"[color],
			elistcnt);
		dlog(fmtbuf);
		whatsup(0);
	}
#endif
}

/*
 * Compute all level 2 combos of frames intersecting spot 'osp'
 * within the frame 'ocbp' and combo value 's'.
 */
void
makecombo2(struct combostr *ocbp, struct spotstr *osp, int off, int s)
{
	struct spotstr *fsp;
	struct combostr *ncbp;
	int f, r, d, c;
	int baseB, fcnt, emask, bmask, n;
	union comboval ocb, fcb;
	struct combostr **scbpp, *fcbp;

	/* try to combine a new frame with those found so far */
	ocb.s = s;
	baseB = ocb.c.a + ocb.c.b - 1;
	fcnt = ocb.c.a - 2;
	emask = fcnt ? ((ocb.c.b ? 0x1E : 0x1F) & ~(1 << off)) : 0;
	for (r = 4; --r >= 0; ) {			/* for each direction */
	    /* don't include frames that overlap in the same direction */
	    if (r == ocbp->c_dir)
		continue;
	    d = dd[r];
	    /*
	     * Frame A combined with B is the same value as B combined with A
	     * so skip frames that have already been processed (MFLAG).
	     * Also skip blocked frames (BFLAG) and frames that are <1,x>
	     * since combining another frame with it isn't valid.
	     */
	    bmask = (BFLAG | FFLAG | MFLAG) << r;
	    fsp = osp;
	    for (f = 0; f < 5; f++, fsp -= d) {		/* for each frame */
		if (fsp->s_occ == BORDER)
		    break;
		if (fsp->s_flg & bmask)
		    continue;

		/* don't include frames of the wrong color */
		fcb.s = fsp->s_fval[curcolor][r].s;
		if (fcb.c.a >= MAXA)
		    continue;

		/*
		 * Get the combo value for this frame.
		 * If this is the end point of the frame,
		 * use the closed ended value for the frame.
		 */
		if (f == 0 && (fcb.c.b || fcb.s == 0x101)) {
		    fcb.c.a++;
		    fcb.c.b = 0;
		}

		/* compute combo value */
		c = fcb.c.a + ocb.c.a - 3;
		if (c > 4)
		    continue;
		n = fcb.c.a + fcb.c.b - 1;
		if (baseB < n)
		    n = baseB;

		/* make a new combo! */
		ncbp = reallocarray(NULL, 3, sizeof(struct combostr));
		if (ncbp == (struct combostr *)NULL)
		    qlog("Memory allocation failure.");
		scbpp = (struct combostr **)(ncbp + 1);
		fcbp = fsp->s_frame[r];
		if (ocbp < fcbp) {
		    scbpp[0] = ocbp;
		    scbpp[1] = fcbp;
		} else {
		    scbpp[0] = fcbp;
		    scbpp[1] = ocbp;
		}
		ncbp->c_combo.c.a = c;
		ncbp->c_combo.c.b = n;
		ncbp->c_link[0] = ocbp;
		ncbp->c_link[1] = fcbp;
		ncbp->c_linkv[0].s = ocb.s;
		ncbp->c_linkv[1].s = fcb.s;
		ncbp->c_voff[0] = off;
		ncbp->c_voff[1] = f;
		ncbp->c_vertex = osp - board;
		ncbp->c_nframes = 2;
		ncbp->c_dir = 0;
		ncbp->c_frameindex = 0;
		ncbp->c_flg = (ocb.c.b) ? C_OPEN_0 : 0;
		if (fcb.c.b)
		    ncbp->c_flg |= C_OPEN_1;
		ncbp->c_framecnt[0] = fcnt;
		ncbp->c_emask[0] = emask;
		ncbp->c_framecnt[1] = fcb.c.a - 2;
		ncbp->c_emask[1] = ncbp->c_framecnt[1] ?
		    ((fcb.c.b ? 0x1E : 0x1F) & ~(1 << f)) : 0;
		combocnt++;

		if (c == 1 && debug > 1) {
		    snprintf(fmtbuf, sizeof fmtbuf,
			"%c c %d %d m %x %x o %d %d",
			"bw"[curcolor],
			ncbp->c_framecnt[0], ncbp->c_framecnt[1],
			ncbp->c_emask[0], ncbp->c_emask[1],
			ncbp->c_voff[0], ncbp->c_voff[1]);
		    dlog(fmtbuf);
		    printcombo(ncbp, fmtbuf, sizeof fmtbuf);
		    dlog(fmtbuf);
		}
		if (c > 1) {
		    /* record the empty spots that will complete this combo */
		    makeempty(ncbp);

		    /* add the new combo to the end of the list */
		    appendcombo(ncbp);
		} else {
		    updatecombo(ncbp, curcolor);
		    free(ncbp);
		    combocnt--;
		}
#ifdef DEBUG
		if (c == 1 && debug > 1 || debug > 5) {
		    markcombo(ncbp);
		    bdisp();
		    whatsup(0);
		    clearcombo(ncbp, 0);
		}
#endif /* DEBUG */
	    }
	}
}

/*
 * Scan the sorted list of frames and try to add a frame to
 * combinations of 'level' number of frames.
 */
void
addframes(int level)
{
	struct combostr *cbp, *ecbp;
	struct spotstr *sp, *fsp;
	struct elist *ep, *nep;
	int i, r, d;
	struct combostr **cbpp, *pcbp;
	union comboval fcb, cb;

	curlevel = level;

	/* scan for combos at empty spots */
	i = curcolor;
	for (sp = &board[PT(T,20)]; --sp >= &board[PT(A,1)]; ) {
		for (ep = sp->s_empty; ep; ep = nep) {
			cbp = ep->e_combo;
			if (cbp->c_combo.s <= sp->s_combo[i].s) {
				if (cbp->c_combo.s != sp->s_combo[i].s) {
					sp->s_combo[i].s = cbp->c_combo.s;
					sp->s_level[i] = cbp->c_nframes;
				} else if (cbp->c_nframes < sp->s_level[i])
					sp->s_level[i] = cbp->c_nframes;
			}
			nep = ep->e_next;
			free(ep);
			elistcnt--;
		}
		sp->s_empty = sp->s_nempty;
		sp->s_nempty = (struct elist *)0;
	}

	/* try to add frames to the uncompleted combos at level curlevel */
	cbp = ecbp = sortframes[curcolor];
	do {
		fsp = &board[cbp->c_vertex];
		r = cbp->c_dir;
		/* skip frames that are part of a <1,x> combo */
		if (fsp->s_flg & (FFLAG << r))
			continue;

		/*
		 * Don't include <1,x> combo frames,
		 * treat it as a closed three in a row instead.
		 */
		fcb.s = fsp->s_fval[curcolor][r].s;
		if (fcb.s == 0x101)
			fcb.s = 0x200;

		/*
		 * If this is an open ended frame, use
		 * the combo value with the end closed.
		 */
		if (fsp->s_occ == EMPTY) {
			if (fcb.c.b) {
				cb.c.a = fcb.c.a + 1;
				cb.c.b = 0;
			} else
				cb.s = fcb.s;
			makecombo(cbp, fsp, 0, cb.s);
		}

		/*
		 * The next four spots are handled the same for both
		 * open and closed ended frames.
		 */
		d = dd[r];
		sp = fsp + d;
		for (i = 1; i < 5; i++, sp += d) {
			if (sp->s_occ != EMPTY)
				continue;
			makecombo(cbp, sp, i, fcb.s);
		}
	} while ((cbp = cbp->c_next) != ecbp);

	/* put all the combos in the hash list on the sorted list */
	cbpp = &hashcombos[FAREA];
	do {
		cbp = *--cbpp;
		if (cbp == (struct combostr *)0)
			continue;
		*cbpp = (struct combostr *)0;
		ecbp = sortcombos;
		if (ecbp == (struct combostr *)0)
			sortcombos = cbp;
		else {
			/* append to sort list */
			pcbp = ecbp->c_prev;
			pcbp->c_next = cbp;
			ecbp->c_prev = cbp->c_prev;
			cbp->c_prev->c_next = ecbp;
			cbp->c_prev = pcbp;
		}
	} while (cbpp != hashcombos);
}

/*
 * Compute all level N combos of frames intersecting spot 'osp'
 * within the frame 'ocbp' and combo value 's'.
 */
void
makecombo(struct combostr *ocbp, struct spotstr *osp, int off, int s)
{
	struct combostr *cbp, *ncbp;
	struct spotstr *sp;
	struct elist *ep;
	int n, c;
	struct elist *nep;
	struct combostr **scbpp;
	int baseB, fcnt, emask, verts;
	union comboval ocb;
	struct ovlp_info vertices[1];

	ocb.s = s;
	baseB = ocb.c.a + ocb.c.b - 1;
	fcnt = ocb.c.a - 2;
	emask = fcnt ? ((ocb.c.b ? 0x1E : 0x1F) & ~(1 << off)) : 0;
	for (ep = osp->s_empty; ep; ep = ep->e_next) {
	    /* check for various kinds of overlap */
	    cbp = ep->e_combo;
	    verts = checkframes(cbp, ocbp, osp, s, vertices);
	    if (verts < 0)
		continue;

	    /* check to see if this frame forms a valid loop */
	    if (verts) {
		sp = &board[vertices[0].o_intersect];
#ifdef DEBUG
		if (sp->s_occ != EMPTY) {
		    snprintf(fmtbuf, sizeof fmtbuf,
			"loop: %c %s", "BW"[curcolor],
			stoc(sp - board));
		    dlog(fmtbuf);
		    whatsup(0);
		}
#endif
		/*
		 * It is a valid loop if the intersection spot
		 * of the frame we are trying to attach is one
		 * of the completion spots of the combostr
		 * we are trying to attach the frame to.
		 */
		for (nep = sp->s_empty; nep; nep = nep->e_next) {
		    if (nep->e_combo == cbp)
			goto fnd;
		    if (nep->e_combo->c_nframes < cbp->c_nframes)
			break;
		}
		/* frame overlaps but not at a valid spot */
		continue;
	    fnd:
		;
	    }

	    /* compute the first half of the combo value */
	    c = cbp->c_combo.c.a + ocb.c.a - verts - 3;
	    if (c > 4)
		continue;

	    /* compute the second half of the combo value */
	    n = ep->e_fval.c.a + ep->e_fval.c.b - 1;
	    if (baseB < n)
		n = baseB;

	    /* make a new combo! */
	    ncbp = malloc(sizeof(struct combostr) +
		(cbp->c_nframes + 1) * sizeof(struct combostr *));
	    if (ncbp == (struct combostr *)NULL)
		qlog("Memory allocation failure.");
	    scbpp = (struct combostr **)(ncbp + 1);
	    if (sortcombo(scbpp, (struct combostr **)(cbp + 1), ocbp)) {
		free(ncbp);
		continue;
	    }
	    combocnt++;

	    ncbp->c_combo.c.a = c;
	    ncbp->c_combo.c.b = n;
	    ncbp->c_link[0] = cbp;
	    ncbp->c_link[1] = ocbp;
	    ncbp->c_linkv[1].s = ocb.s;
	    ncbp->c_voff[1] = off;
	    ncbp->c_vertex = osp - board;
	    ncbp->c_nframes = cbp->c_nframes + 1;
	    ncbp->c_flg = ocb.c.b ? C_OPEN_1 : 0;
	    ncbp->c_frameindex = ep->e_frameindex;
	    /*
	     * Update the completion spot mask of the frame we
	     * are attaching 'ocbp' to so the intersection isn't
	     * listed twice.
	     */
	    ncbp->c_framecnt[0] = ep->e_framecnt;
	    ncbp->c_emask[0] = ep->e_emask;
	    if (verts) {
		ncbp->c_flg |= C_LOOP;
		ncbp->c_dir = vertices[0].o_frameindex;
		ncbp->c_framecnt[1] = fcnt - 1;
		if (ncbp->c_framecnt[1]) {
		    n = (vertices[0].o_intersect - ocbp->c_vertex) /
			dd[ocbp->c_dir];
		    ncbp->c_emask[1] = emask & ~(1 << n);
		} else
		    ncbp->c_emask[1] = 0;
		ncbp->c_voff[0] = vertices[0].o_off;
	    } else {
		ncbp->c_dir = 0;
		ncbp->c_framecnt[1] = fcnt;
		ncbp->c_emask[1] = emask;
		ncbp->c_voff[0] = ep->e_off;
	    }

	    if (c == 1 && debug > 1) {
		snprintf(fmtbuf, sizeof fmtbuf,
		    "%c v%d i%d d%d c %d %d m %x %x o %d %d",
		    "bw"[curcolor], verts, ncbp->c_frameindex, ncbp->c_dir,
		    ncbp->c_framecnt[0], ncbp->c_framecnt[1],
		    ncbp->c_emask[0], ncbp->c_emask[1],
		    ncbp->c_voff[0], ncbp->c_voff[1]);
		dlog(fmtbuf);
		printcombo(ncbp, fmtbuf, sizeof fmtbuf);
		dlog(fmtbuf);
	    }
	    if (c > 1) {
		/* record the empty spots that will complete this combo */
		makeempty(ncbp);
		combolen++;
	    } else {
		/* update board values */
		updatecombo(ncbp, curcolor);
	    }
#ifdef DEBUG
	    if (c == 1 && debug > 1 || debug > 4) {
		markcombo(ncbp);
		bdisp();
		whatsup(0);
		clearcombo(ncbp, 0);
	    }
#endif /* DEBUG */
	}
}

#define MAXDEPTH	100
struct elist	einfo[MAXDEPTH];
struct combostr	*ecombo[MAXDEPTH];	/* separate from elist to save space */

/*
 * Add the combostr 'ocbp' to the empty spots list for each empty spot
 * in 'ocbp' that will complete the combo.
 */
void
makeempty(struct combostr *ocbp)
{
	struct combostr *cbp, **cbpp;
	struct elist *ep, *nep;
	struct spotstr *sp;
	int s, d, m, emask, i;
	int nframes;

	if (debug > 2) {
		snprintf(fmtbuf, sizeof fmtbuf, "E%c ", "bw"[curcolor]);
		printcombo(ocbp, fmtbuf + 3, sizeof fmtbuf - 3);
		dlog(fmtbuf);
	}

	/* should never happen but check anyway */
	if ((nframes = ocbp->c_nframes) >= MAXDEPTH)
		return;

	/*
	 * The lower level combo can be pointed to by more than one
	 * higher level 'struct combostr' so we can't modify the
	 * lower level. Therefore, higher level combos store the
	 * real mask of the lower level frame in c_emask[0] and the
	 * frame number in c_frameindex.
	 *
	 * First we traverse the tree from top to bottom and save the
	 * connection info. Then we traverse the tree from bottom to
	 * top overwriting lower levels with the newer emask information.
	 */
	ep = &einfo[nframes];
	cbpp = &ecombo[nframes];
	for (cbp = ocbp; cbp->c_link[1] != NULL; cbp = cbp->c_link[0]) {
		ep--;
		ep->e_combo = cbp;
		*--cbpp = cbp->c_link[1];
		ep->e_off = cbp->c_voff[1];
		ep->e_frameindex = cbp->c_frameindex;
		ep->e_fval.s = cbp->c_linkv[1].s;
		ep->e_framecnt = cbp->c_framecnt[1];
		ep->e_emask = cbp->c_emask[1];
	}
	cbp = ep->e_combo;
	ep--;
	ep->e_combo = cbp;
	*--cbpp = cbp->c_link[0];
	ep->e_off = cbp->c_voff[0];
	ep->e_frameindex = 0;
	ep->e_fval.s = cbp->c_linkv[0].s;
	ep->e_framecnt = cbp->c_framecnt[0];
	ep->e_emask = cbp->c_emask[0];

	/* now update the emask info */
	s = 0;
	for (i = 2, ep += 2; i < nframes; i++, ep++) {
		cbp = ep->e_combo;
		nep = &einfo[ep->e_frameindex];
		nep->e_framecnt = cbp->c_framecnt[0];
		nep->e_emask = cbp->c_emask[0];

		if (cbp->c_flg & C_LOOP) {
			s++;
			/*
			 * Account for the fact that this frame connects
			 * to a previous one (thus forming a loop).
			 */
			nep = &einfo[cbp->c_dir];
			if (--nep->e_framecnt)
				nep->e_emask &= ~(1 << cbp->c_voff[0]);
			else
				nep->e_emask = 0;
		}
	}

	/*
	 * We only need to update the emask values of "complete" loops
	 * to include the intersection spots.
	 */
	if (s && ocbp->c_combo.c.a == 2) {
		/* process loops from the top down */
		ep = &einfo[nframes];
		do {
			ep--;
			cbp = ep->e_combo;
			if (!(cbp->c_flg & C_LOOP))
				continue;

			/*
			 * Update the emask values to include the
			 * intersection spots.
			 */
			nep = &einfo[cbp->c_dir];
			nep->e_framecnt = 1;
			nep->e_emask = 1 << cbp->c_voff[0];
			ep->e_framecnt = 1;
			ep->e_emask = 1 << ep->e_off;
			ep = &einfo[ep->e_frameindex];
			do {
				ep->e_framecnt = 1;
				ep->e_emask = 1 << ep->e_off;
				ep = &einfo[ep->e_frameindex];
			} while (ep > nep);
		} while (ep != einfo);
	}

	/* check all the frames for completion spots */
	for (i = 0, ep = einfo, cbpp = ecombo; i < nframes; i++, ep++, cbpp++) {
		/* skip this frame if there are no incomplete spots in it */
		if ((emask = ep->e_emask) == 0)
			continue;
		cbp = *cbpp;
		sp = &board[cbp->c_vertex];
		d = dd[cbp->c_dir];
		for (s = 0, m = 1; s < 5; s++, sp += d, m <<= 1) {
			if (sp->s_occ != EMPTY || !(emask & m))
				continue;

			/* add the combo to the list of empty spots */
			nep = malloc(sizeof(struct elist));
			if (nep == (struct elist *)NULL)
				qlog("Memory allocation failure.");
			nep->e_combo = ocbp;
			nep->e_off = s;
			nep->e_frameindex = i;
			if (ep->e_framecnt > 1) {
				nep->e_framecnt = ep->e_framecnt - 1;
				nep->e_emask = emask & ~m;
			} else {
				nep->e_framecnt = 0;
				nep->e_emask = 0;
			}
			nep->e_fval.s = ep->e_fval.s;
			if (debug > 2) {
				snprintf(fmtbuf, sizeof fmtbuf,
					"e %s o%d i%d c%d m%x %x",
					stoc(sp - board),
					nep->e_off,
					nep->e_frameindex,
					nep->e_framecnt,
					nep->e_emask,
					nep->e_fval.s);
				dlog(fmtbuf);
			}

			/* sort by the number of frames in the combo */
			nep->e_next = sp->s_nempty;
			sp->s_nempty = nep;
			elistcnt++;
		}
	}
}

/*
 * Update the board value based on the combostr.
 * This is called only if 'cbp' is a <1,x> combo.
 * We handle things differently depending on whether the next move
 * would be trying to "complete" the combo or trying to block it.
 */
void
updatecombo(struct combostr *cbp, int color)
{
	struct spotstr *sp;
	struct combostr *tcbp;
	int i, d;
	int nframes, s, flg = 0;
	union comboval cb;

	/* save the top level value for the whole combo */
	cb.c.a = cbp->c_combo.c.a;
	nframes = cbp->c_nframes;

	if (color != nextcolor)
		memset(tmpmap, 0, sizeof(tmpmap));

	for (; (tcbp = cbp->c_link[1]) != NULL; cbp = cbp->c_link[0]) {
		flg = cbp->c_flg;
		cb.c.b = cbp->c_combo.c.b;
		if (color == nextcolor) {
			/* update the board value for the vertex */
			sp = &board[cbp->c_vertex];
			sp->s_nforce[color]++;
			if (cb.s <= sp->s_combo[color].s) {
				if (cb.s != sp->s_combo[color].s) {
					sp->s_combo[color].s = cb.s;
					sp->s_level[color] = nframes;
				} else if (nframes < sp->s_level[color])
					sp->s_level[color] = nframes;
			}
		} else {
			/* update the board values for each spot in frame */
			sp = &board[s = tcbp->c_vertex];
			d = dd[tcbp->c_dir];
			i = (flg & C_OPEN_1) ? 6 : 5;
			for (; --i >= 0; sp += d, s += d) {
				if (sp->s_occ != EMPTY)
					continue;
				sp->s_nforce[color]++;
				if (cb.s <= sp->s_combo[color].s) {
					if (cb.s != sp->s_combo[color].s) {
						sp->s_combo[color].s = cb.s;
						sp->s_level[color] = nframes;
					} else if (nframes < sp->s_level[color])
						sp->s_level[color] = nframes;
				}
				BIT_SET(tmpmap, s);
			}
		}

		/* mark the frame as being part of a <1,x> combo */
		board[tcbp->c_vertex].s_flg |= FFLAG << tcbp->c_dir;
	}

	if (color != nextcolor) {
		/* update the board values for each spot in frame */
		sp = &board[s = cbp->c_vertex];
		d = dd[cbp->c_dir];
		i = (flg & C_OPEN_0) ? 6 : 5;
		for (; --i >= 0; sp += d, s += d) {
			if (sp->s_occ != EMPTY)
				continue;
			sp->s_nforce[color]++;
			if (cb.s <= sp->s_combo[color].s) {
				if (cb.s != sp->s_combo[color].s) {
					sp->s_combo[color].s = cb.s;
					sp->s_level[color] = nframes;
				} else if (nframes < sp->s_level[color])
					sp->s_level[color] = nframes;
			}
			BIT_SET(tmpmap, s);
		}
		if (nforce == 0)
			memcpy(forcemap, tmpmap, sizeof(tmpmap));
		else {
			for (i = 0; i < MAPSZ; i++)
				forcemap[i] &= tmpmap[i];
		}
		nforce++;
	}

	/* mark the frame as being part of a <1,x> combo */
	board[cbp->c_vertex].s_flg |= FFLAG << cbp->c_dir;
}

/*
 * Add combo to the end of the list.
 */
void
appendcombo(struct combostr *cbp)
{
	struct combostr *pcbp, *ncbp;

	combolen++;
	ncbp = sortcombos;
	if (ncbp == (struct combostr *)0) {
		sortcombos = cbp;
		cbp->c_next = cbp;
		cbp->c_prev = cbp;
		return;
	}
	pcbp = ncbp->c_prev;
	cbp->c_next = ncbp;
	cbp->c_prev = pcbp;
	ncbp->c_prev = cbp;
	pcbp->c_next = cbp;
}

/*
 * Return zero if it is valid to combine frame 'fcbp' with the frames
 * in 'cbp' and forms a linked chain of frames (i.e., a tree; no loops).
 * Return positive if combining frame 'fcbp' to the frames in 'cbp'
 * would form some kind of valid loop. Also return the intersection spots
 * in 'vertices[]' beside the known intersection at spot 'osp'.
 * Return -1 if 'fcbp' should not be combined with 'cbp'.
 * 's' is the combo value for frame 'fcpb'.
 */
int
checkframes(struct combostr *cbp, struct combostr *fcbp, struct spotstr *osp,
    int s, struct ovlp_info *vertices)
{
	struct combostr *tcbp, *lcbp = NULL;
	int i, n, mask, flg, verts, idx, fcnt;
	union comboval cb;
	u_char *str;
	short *ip;

	cb.s = s;
	fcnt = cb.c.a - 2;
	verts = 0;
	flg = 0;
	idx = cbp->c_nframes;
	n = (fcbp - frames) * FAREA;
	str = &overlap[n];
	ip = &intersect[n];
	/*
	 * i == which overlap bit to test based on whether 'fcbp' is
	 * an open or closed frame.
	 */
	i = cb.c.b ? 2 : 0;
	for (; (tcbp = cbp->c_link[1]) != NULL; lcbp = cbp, cbp = cbp->c_link[0]) {
		if (tcbp == fcbp)
			return (-1);	/* fcbp is already included */

		/* check for intersection of 'tcbp' with 'fcbp' */
		idx--;
		mask = str[tcbp - frames];
		flg = cbp->c_flg;
		n = i + ((flg & C_OPEN_1) != 0);
		if (mask & (1 << n)) {
			/*
			 * The two frames are not independent if they
			 * both lie in the same line and intersect at
			 * more than one point.
			 */
			if (tcbp->c_dir == fcbp->c_dir && (mask & (0x10 << n)))
				return (-1);
			/*
			 * If this is not the spot we are attaching
			 * 'fcbp' to and it is a reasonable intersection
			 * spot, then there might be a loop.
			 */
			n = ip[tcbp - frames];
			if (osp != &board[n]) {
				/* check to see if this is a valid loop */
				if (verts)
					return (-1);
				if (fcnt == 0 || cbp->c_framecnt[1] == 0)
					return (-1);
				/*
				 * Check to be sure the intersection is not
				 * one of the end points if it is an open
				 * ended frame.
				 */
				if ((flg & C_OPEN_1) &&
				    (n == tcbp->c_vertex ||
				     n == tcbp->c_vertex + 5 * dd[tcbp->c_dir]))
					return (-1);	/* invalid overlap */
				if (cb.c.b &&
				    (n == fcbp->c_vertex ||
				     n == fcbp->c_vertex + 5 * dd[fcbp->c_dir]))
					return (-1);	/* invalid overlap */

				vertices->o_intersect = n;
				vertices->o_fcombo = cbp;
				vertices->o_link = 1;
				vertices->o_off = (n - tcbp->c_vertex) /
					dd[tcbp->c_dir];
				vertices->o_frameindex = idx;
				verts++;
			}
		}
		n = i + ((flg & C_OPEN_0) != 0);
	}
	if (cbp == fcbp)
		return (-1);	/* fcbp is already included */

	/* check for intersection of 'cbp' with 'fcbp' */
	mask = str[cbp - frames];
	if (mask & (1 << n)) {
		/*
		 * The two frames are not independent if they
		 * both lie in the same line and intersect at
		 * more than one point.
		 */
		if (cbp->c_dir == fcbp->c_dir && (mask & (0x10 << n)))
			return (-1);
		/*
		 * If this is not the spot we are attaching
		 * 'fcbp' to and it is a reasonable intersection
		 * spot, then there might be a loop.
		 */
		n = ip[cbp - frames];
		if (osp != &board[n]) {
			/* check to see if this is a valid loop */
			if (verts)
				return (-1);
			if (fcnt == 0 || lcbp->c_framecnt[0] == 0)
				return (-1);
			/*
			 * Check to be sure the intersection is not
			 * one of the end points if it is an open
			 * ended frame.
			 */
			if ((flg & C_OPEN_0) &&
			    (n == cbp->c_vertex ||
			     n == cbp->c_vertex + 5 * dd[cbp->c_dir]))
				return (-1);	/* invalid overlap */
			if (cb.c.b &&
			    (n == fcbp->c_vertex ||
			     n == fcbp->c_vertex + 5 * dd[fcbp->c_dir]))
				return (-1);	/* invalid overlap */

			vertices->o_intersect = n;
			vertices->o_fcombo = lcbp;
			vertices->o_link = 0;
			vertices->o_off = (n - cbp->c_vertex) /
				dd[cbp->c_dir];
			vertices->o_frameindex = 0;
			verts++;
		}
	}
	return (verts);
}

/*
 * Merge sort the frame 'fcbp' and the sorted list of frames 'cbpp' and
 * store the result in 'scbpp'. 'curlevel' is the size of the 'cbpp' array.
 * Return true if this list of frames is already in the hash list.
 * Otherwise, add the new combo to the hash list.
 */
int
sortcombo(struct combostr **scbpp, struct combostr **cbpp,
    struct combostr *fcbp)
{
	struct combostr **spp, **cpp;
	struct combostr *cbp, *ecbp;
	int n, inx;

#ifdef DEBUG
	if (debug > 3) {
		char *str;

		snprintf(fmtbuf, sizeof fmtbuf,
			"sortc: %s%c l%d", stoc(fcbp->c_vertex),
			pdir[fcbp->c_dir], curlevel);
		dlog(fmtbuf);
		str = fmtbuf;
		for (cpp = cbpp; cpp < cbpp + curlevel; cpp++) {
			snprintf(str, fmtbuf + sizeof fmtbuf - str,
				" %s%c", stoc((*cpp)->c_vertex),
				pdir[(*cpp)->c_dir]);
			str += strlen(str);
		}
		dlog(fmtbuf);
	}
#endif /* DEBUG */

	/* first build the new sorted list */
	n = curlevel + 1;
	spp = scbpp + n;
	cpp = cbpp + curlevel;
	do {
		cpp--;
		if (fcbp > *cpp) {
			*--spp = fcbp;
			do
				*--spp = *cpp;
			while (cpp-- != cbpp);
			goto inserted;
		}
		*--spp = *cpp;
	} while (cpp != cbpp);
	*--spp = fcbp;
inserted:

	/* now check to see if this list of frames has already been seen */
	cbp = hashcombos[inx = *scbpp - frames];
	if (cbp == (struct combostr *)0) {
		/*
		 * Easy case, this list hasn't been seen.
		 * Add it to the hash list.
		 */
		fcbp = (struct combostr *)
			((char *)scbpp - sizeof(struct combostr));
		hashcombos[inx] = fcbp;
		fcbp->c_next = fcbp->c_prev = fcbp;
		return (0);
	}
	ecbp = cbp;
	do {
		cbpp = (struct combostr **)(cbp + 1);
		cpp = cbpp + n;
		spp = scbpp + n;
		cbpp++;	/* first frame is always the same */
		do {
			if (*--spp != *--cpp)
				goto next;
		} while (cpp != cbpp);
		/* we found a match */
#ifdef DEBUG
		if (debug > 3) {
			char *str;

			snprintf(fmtbuf, sizeof fmtbuf, "sort1: n%d", n);
			dlog(fmtbuf);
			str = fmtbuf;
			for (cpp = scbpp; cpp < scbpp + n; cpp++) {
				snprintf(str, fmtbuf + sizeof fmtbuf - str,
					" %s%c", stoc((*cpp)->c_vertex),
					pdir[(*cpp)->c_dir]);
				str += strlen(str);
			}
			dlog(fmtbuf);
			printcombo(cbp, fmtbuf, sizeof fmtbuf);
			dlog(fmtbuf);
			str = fmtbuf;
			cbpp--;
			for (cpp = cbpp; cpp < cbpp + n; cpp++) {
				snprintf(str, fmtbuf + sizeof fmtbuf - str,
					" %s%c", stoc((*cpp)->c_vertex),
					pdir[(*cpp)->c_dir]);
				str += strlen(str);
			}
			dlog(fmtbuf);
		}
#endif /* DEBUG */
		return (1);
	next:
		;
	} while ((cbp = cbp->c_next) != ecbp);
	/*
	 * This list of frames hasn't been seen.
	 * Add it to the hash list.
	 */
	ecbp = cbp->c_prev;
	fcbp = (struct combostr *)((char *)scbpp - sizeof(struct combostr));
	fcbp->c_next = cbp;
	fcbp->c_prev = ecbp;
	cbp->c_prev = fcbp;
	ecbp->c_next = fcbp;
	return (0);
}

/*
 * Print the combo into string 'str'.
 */
void
printcombo(struct combostr *cbp, char *str, size_t strl)
{
	char *basestr = str;
	struct combostr *tcbp;

	snprintf(str, strl, "%x/%d", cbp->c_combo.s, cbp->c_nframes);
	str += strlen(str);
	for (; (tcbp = cbp->c_link[1]) != NULL; cbp = cbp->c_link[0]) {
		snprintf(str, basestr + strl - str,
			" %s%c%x", stoc(tcbp->c_vertex), pdir[tcbp->c_dir],
			cbp->c_flg);
		str += strlen(str);
	}
	snprintf(str, basestr + strl - str,
		" %s%c", stoc(cbp->c_vertex), pdir[cbp->c_dir]);
}

#ifdef DEBUG
void
markcombo(struct combostr *ocbp)
{
	struct combostr *cbp, *tcbp, **cbpp;
	struct elist *ep, *nep, **epp;
	struct spotstr *sp;
	int s, d, m, i;
	int nframes;
	int r, n, flg, cmask, omask;

	/* should never happen but check anyway */
	if ((nframes = ocbp->c_nframes) >= MAXDEPTH)
		return;

	/*
	 * The lower level combo can be pointed to by more than one
	 * higher level 'struct combostr' so we can't modify the
	 * lower level. Therefore, higher level combos store the
	 * real mask of the lower level frame in c_emask[0] and the
	 * frame number in c_frameindex.
	 *
	 * First we traverse the tree from top to bottom and save the
	 * connection info. Then we traverse the tree from bottom to
	 * top overwriting lower levels with the newer emask information.
	 */
	ep = &einfo[nframes];
	cbpp = &ecombo[nframes];
	for (cbp = ocbp; tcbp = cbp->c_link[1]; cbp = cbp->c_link[0]) {
		ep--;
		ep->e_combo = cbp;
		*--cbpp = cbp->c_link[1];
		ep->e_off = cbp->c_voff[1];
		ep->e_frameindex = cbp->c_frameindex;
		ep->e_fval.s = cbp->c_linkv[1].s;
		ep->e_framecnt = cbp->c_framecnt[1];
		ep->e_emask = cbp->c_emask[1];
	}
	cbp = ep->e_combo;
	ep--;
	ep->e_combo = cbp;
	*--cbpp = cbp->c_link[0];
	ep->e_off = cbp->c_voff[0];
	ep->e_frameindex = 0;
	ep->e_fval.s = cbp->c_linkv[0].s;
	ep->e_framecnt = cbp->c_framecnt[0];
	ep->e_emask = cbp->c_emask[0];

	/* now update the emask info */
	s = 0;
	for (i = 2, ep += 2; i < nframes; i++, ep++) {
		cbp = ep->e_combo;
		nep = &einfo[ep->e_frameindex];
		nep->e_framecnt = cbp->c_framecnt[0];
		nep->e_emask = cbp->c_emask[0];

		if (cbp->c_flg & C_LOOP) {
			s++;
			/*
			 * Account for the fact that this frame connects
			 * to a previous one (thus forming a loop).
			 */
			nep = &einfo[cbp->c_dir];
			if (--nep->e_framecnt)
				nep->e_emask &= ~(1 << cbp->c_voff[0]);
			else
				nep->e_emask = 0;
		}
	}

	/*
	 * We only need to update the emask values of "complete" loops
	 * to include the intersection spots.
	 */
	if (s && ocbp->c_combo.c.a == 2) {
		/* process loops from the top down */
		ep = &einfo[nframes];
		do {
			ep--;
			cbp = ep->e_combo;
			if (!(cbp->c_flg & C_LOOP))
				continue;

			/*
			 * Update the emask values to include the
			 * intersection spots.
			 */
			nep = &einfo[cbp->c_dir];
			nep->e_framecnt = 1;
			nep->e_emask = 1 << cbp->c_voff[0];
			ep->e_framecnt = 1;
			ep->e_emask = 1 << ep->e_off;
			ep = &einfo[ep->e_frameindex];
			do {
				ep->e_framecnt = 1;
				ep->e_emask = 1 << ep->e_off;
				ep = &einfo[ep->e_frameindex];
			} while (ep > nep);
		} while (ep != einfo);
	}

	/* mark all the frames with the completion spots */
	for (i = 0, ep = einfo, cbpp = ecombo; i < nframes; i++, ep++, cbpp++) {
		m = ep->e_emask;
		cbp = *cbpp;
		sp = &board[cbp->c_vertex];
		d = dd[s = cbp->c_dir];
		cmask = CFLAG << s;
		omask = (IFLAG | CFLAG) << s;
		s = ep->e_fval.c.b ? 6 : 5;
		for (; --s >= 0; sp += d, m >>= 1)
			sp->s_flg |= (m & 1) ? omask : cmask;
	}
}

void
clearcombo(struct combostr *cbp, int open)
{
	struct spotstr *sp;
	struct combostr *tcbp;
	int d, n, mask;

	for (; tcbp = cbp->c_link[1]; cbp = cbp->c_link[0]) {
		clearcombo(tcbp, cbp->c_flg & C_OPEN_1);
		open = cbp->c_flg & C_OPEN_0;
	}
	sp = &board[cbp->c_vertex];
	d = dd[n = cbp->c_dir];
	mask = ~((IFLAG | CFLAG) << n);
	n = open ? 6 : 5;
	for (; --n >= 0; sp += d)
		sp->s_flg &= mask;
}

int
list_eq(struct combostr **scbpp, struct combostr **cbpp, int n)
{
	struct combostr **spp, **cpp;

	spp = scbpp + n;
	cpp = cbpp + n;
	do {
		if (*--spp != *--cpp)
			return (0);
	} while (cpp != cbpp);
	/* we found a match */
	return (1);
}
#endif /* DEBUG */
