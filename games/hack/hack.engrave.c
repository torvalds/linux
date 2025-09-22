/*	$OpenBSD: hack.engrave.c,v 1.9 2016/01/09 21:54:11 mestre Exp $	*/

/*
 * Copyright (c) 1985, Stichting Centrum voor Wiskunde en Informatica,
 * Amsterdam
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of the Stichting Centrum voor Wiskunde en
 * Informatica, nor the names of its contributors may be used to endorse or
 * promote products derived from this software without specific prior
 * written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1982 Jay Fenlason <hack@gnu.org>
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>

#include "hack.h"

extern char *nomovemsg;
extern char nul[];
extern struct obj zeroobj;
struct engr {
	struct engr *nxt_engr;
	char *engr_txt;
	xchar engr_x, engr_y;
	unsigned engr_lth;	/* for save & restore; not length of text */
	long engr_time;	/* moment engraving was (will be) finished */
	xchar engr_type;
#define	DUST	1
#define	ENGRAVE	2
#define	BURN	3
} *head_engr;

static void del_engr(struct engr *);

struct engr *
engr_at(xchar x, xchar y)
{
	struct engr *ep = head_engr;

	while(ep) {
		if(x == ep->engr_x && y == ep->engr_y)
			return(ep);
		ep = ep->nxt_engr;
	}
	return((struct engr *) 0);
}

int
sengr_at(char *s, xchar x, xchar y)
{
	struct engr *ep = engr_at(x,y);
	char *t;
	int n;

	if(ep && ep->engr_time <= moves) {
		t = ep->engr_txt;
/*
		if(!strcmp(s,t)) return(1);
*/
		n = strlen(s);
		while(*t) {
			if(!strncmp(s,t,n)) return(1);
			t++;
		}
	}
	return(0);
}

void
u_wipe_engr(int cnt)
{
	if(!u.uswallow && !Levitation)
		wipe_engr_at(u.ux, u.uy, cnt);
}

void
wipe_engr_at(xchar x, xchar y, xchar cnt)
{
	struct engr *ep = engr_at(x,y);
	int lth,pos;
	char ch;

	if(ep){
		if((ep->engr_type != DUST) || Levitation) {
			cnt = rn2(1 + 50/(cnt+1)) ? 0 : 1;
		}
		lth = strlen(ep->engr_txt);
		if(lth && cnt > 0 ) {
			while(cnt--) {
				pos = rn2(lth);
				if((ch = ep->engr_txt[pos]) == ' ')
					continue;
				ep->engr_txt[pos] = (ch != '?') ? '?' : ' ';
			}
		}
		while(lth && ep->engr_txt[lth-1] == ' ')
			ep->engr_txt[--lth] = 0;
		while(ep->engr_txt[0] == ' ')
			ep->engr_txt++;
		if(!ep->engr_txt[0]) del_engr(ep);
	}
}

void
read_engr_at(int x, int y)
{
	struct engr *ep = engr_at(x,y);

	if(ep && ep->engr_txt[0]) {
	    switch(ep->engr_type) {
	    case DUST:
		pline("Something is written here in the dust.");
		break;
	    case ENGRAVE:
		pline("Something is engraved here on the floor.");
		break;
	    case BURN:
		pline("Some text has been burned here in the floor.");
		break;
	    default:
		impossible("Something is written in a very strange way.");
	    }
	    pline("You read: \"%s\".", ep->engr_txt);
	}
}

void
make_engr_at(int x, int y, char *s)
{
	struct engr *ep;
	size_t len = strlen(s) + 1;

	if ((ep = engr_at(x,y)))
	    del_engr(ep);
	ep = (struct engr *)
	    alloc((unsigned)(sizeof(struct engr) + len));
	ep->nxt_engr = head_engr;
	head_engr = ep;
	ep->engr_x = x;
	ep->engr_y = y;
	ep->engr_txt = (char *)(ep + 1);
	(void) strlcpy(ep->engr_txt, s, len);
	ep->engr_time = 0;
	ep->engr_type = DUST;
	ep->engr_lth = len;
}

int
doengrave(void)
{
	int len;
	char *sp;
	struct engr *ep, *oep = engr_at(u.ux,u.uy);
	char buf[BUFSZ];
	xchar type;
	int spct;		/* number of leading spaces */
	struct obj *otmp;

	multi = 0;

	if(u.uswallow) {
		pline("You're joking. Hahaha!");	/* riv05!a3 */
		return(0);
	}

	/* one may write with finger, weapon or wand */
	otmp = getobj("#-)/", "write with");
	if(!otmp) return(0);

	if(otmp == &zeroobj)
		otmp = 0;
	if(otmp && otmp->otyp == WAN_FIRE && otmp->spe) {
		type = BURN;
		otmp->spe--;
	} else {
		/* first wield otmp */
		if(otmp != uwep) {
			if(uwep && uwep->cursed) {
			    /* Andreas Bormann */
			    pline("Since your weapon is welded to your hand,");
			    pline("you use the %s.", aobjnam(uwep, NULL));
			    otmp = uwep;
			} else {
			    if(!otmp)
				pline("You are now empty-handed.");
			    else if(otmp->cursed)
				pline("The %s %s to your hand!",
				    aobjnam(otmp, "weld"),
				    (otmp->quan == 1) ? "itself" : "themselves");
			    else
				pline("You now wield %s.", doname(otmp));
			    setuwep(otmp);
			}
		}

		if(!otmp)
			type = DUST;
		else
		if(otmp->otyp == DAGGER || otmp->otyp == TWO_HANDED_SWORD ||
		otmp->otyp == CRYSKNIFE ||
		otmp->otyp == LONG_SWORD || otmp->otyp == AXE) {
			type = ENGRAVE;
			if((int)otmp->spe <= -3) {
				type = DUST;
				pline("Your %s too dull for engraving.",
					aobjnam(otmp, "are"));
				if(oep && oep->engr_type != DUST) return(1);
			}
		} else	type = DUST;
	}
	if(Levitation && type != BURN){		/* riv05!a3 */
		pline("You can't reach the floor!");
		return(1);
	}
	if(oep && oep->engr_type == DUST){
		  pline("You wipe out the message that was written here.");
		  del_engr(oep);
		  oep = 0;
	}
	if(type == DUST && oep){
	pline("You cannot wipe out the message that is %s in the rock.",
		    (oep->engr_type == BURN) ? "burned" : "engraved");
		  return(1);
	}

	pline("What do you want to %s on the floor here? ",
	  (type == ENGRAVE) ? "engrave" : (type == BURN) ? "burn" : "write");
	getlin(buf);
	clrlin();
	spct = 0;
	sp = buf;
	while(*sp == ' ') spct++, sp++;
	len = strlen(sp);
	if(!len || *buf == '\033') {
		if(type == BURN) otmp->spe++;
		return(0);
	}
	
	switch(type) {
	case DUST:
	case BURN:
		if(len > 15) {
			multi = -(len/10);
			nomovemsg = "You finished writing.";
		}
		break;
	case ENGRAVE:		/* here otmp != 0 */
		{	int len2 = (otmp->spe + 3) * 2 + 1;

			pline("Your %s dull.", aobjnam(otmp, "get"));
			if(len2 < len) {
				len = len2;
				sp[len] = 0;
				otmp->spe = -3;
				nomovemsg = "You cannot engrave more.";
			} else {
				otmp->spe -= len/2;
				nomovemsg = "You finished engraving.";
			}
			multi = -len;
		}
		break;
	}
	if(oep) len += strlen(oep->engr_txt) + spct;
	ep = (struct engr *) alloc((unsigned)(sizeof(struct engr) + len + 1));
	ep->nxt_engr = head_engr;
	head_engr = ep;
	ep->engr_x = u.ux;
	ep->engr_y = u.uy;
	sp = (char *)(ep + 1);	/* (char *)ep + sizeof(struct engr) */
	ep->engr_txt = sp;
	if(oep) {
		(void) strlcpy(sp, oep->engr_txt, len + 1);
		(void) strlcat(sp, buf, len + 1);
		del_engr(oep);
	} else
		(void) strlcpy(sp, buf, len + 1);
	ep->engr_lth = len+1;
	ep->engr_type = type;
	ep->engr_time = moves-multi;

	/* kludge to protect pline against excessively long texts */
	if(len > BUFSZ-20) sp[BUFSZ-20] = 0;

	return(1);
}

void
save_engravings(int fd)
{
	struct engr *ep = head_engr;

	while(ep) {
		if(!ep->engr_lth || !ep->engr_txt[0]){
			ep = ep->nxt_engr;
			continue;
		}
		bwrite(fd,  &(ep->engr_lth), sizeof(ep->engr_lth));
		bwrite(fd, ep, sizeof(struct engr) + ep->engr_lth);
		ep = ep->nxt_engr;
	}
	bwrite(fd, nul, sizeof(unsigned));
	head_engr = 0;
}

void
rest_engravings(int fd)
{
	struct engr *ep;
	unsigned lth;

	head_engr = 0;
	while(1) {
		mread(fd, (char *) &lth, sizeof(unsigned));
		if(lth == 0) return;
		ep = (struct engr *) alloc(sizeof(struct engr) + lth);
		mread(fd, (char *) ep, sizeof(struct engr) + lth);
		ep->nxt_engr = head_engr;
		ep->engr_txt = (char *) (ep + 1);	/* Andreas Bormann */
		head_engr = ep;
	}
}

static void
del_engr(struct engr *ep)
{
	struct engr *ept;

	if(ep == head_engr)
		head_engr = ep->nxt_engr;
	else {
		for(ept = head_engr; ept; ept = ept->nxt_engr) {
			if(ept->nxt_engr == ep) {
				ept->nxt_engr = ep->nxt_engr;
				goto fnd;
			}
		}
		impossible("Error in del_engr?");
		return;
	fnd:	;
	}
	free(ep);
}
