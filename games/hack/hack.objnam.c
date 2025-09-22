/*	$OpenBSD: hack.objnam.c,v 1.12 2023/09/06 11:53:56 jsg Exp $	*/

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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "hack.h"

#define	PREFIX	15
extern int bases[];

static char bufr[BUFSZ];

static char *sitoa(int);

char *
strprepend(char *s, char *pref)
{
	int i = strlen(pref);

	if(i > PREFIX) {
		pline("WARNING: prefix too short.");
		return(s);
	}
	s -= i;
	(void) strncpy(s, pref, i);	/* do not copy trailing 0 */
	return(s);
}

static char *
sitoa(int a)
{
	static char buf[13];

	snprintf(buf, sizeof buf, (a < 0) ? "%d" : "+%d", a);
	return(buf);
}

char *
typename(int otyp)
{
	static char buf[BUFSZ];
	struct objclass *ocl = &objects[otyp];
	char *an = ocl->oc_name;
	char *dn = ocl->oc_descr;
	char *un = ocl->oc_uname;
	char *bp;
	int nn = ocl->oc_name_known;

	switch(ocl->oc_olet) {
	case POTION_SYM:
		strlcpy(buf, "potion", sizeof buf);
		break;
	case SCROLL_SYM:
		strlcpy(buf, "scroll", sizeof buf);
		break;
	case WAND_SYM:
		strlcpy(buf, "wand", sizeof buf);
		break;
	case RING_SYM:
		strlcpy(buf, "ring", sizeof buf);
		break;
	default:
		if(nn) {
			strlcpy(buf, an, sizeof buf);
			if(otyp >= TURQUOISE && otyp <= JADE)
				strlcat(buf, " stone", sizeof buf);
			if(un) {
				bp = eos(buf);
				snprintf(bp, buf + sizeof buf - bp,
				    " called %s", un);
			}
			if(dn) {
				bp = eos(buf);
				snprintf(bp, buf + sizeof buf - bp,
				    " (%s)", dn);
			}
		} else {
			strlcpy(buf, dn ? dn : an, sizeof buf);
			if(ocl->oc_olet == GEM_SYM)
				strlcat(buf, " gem", sizeof buf);
			if(un) {
				bp = eos(buf);
				snprintf(bp, buf + sizeof buf - bp,
				    " called %s", un);
			}
		}
		return(buf);
	}
	/* here for ring/scroll/potion/wand */
	if(nn) {
		bp = eos(buf);
		snprintf(bp, buf + sizeof buf - bp, " of %s", an);
	}
	if(un) {
		bp = eos(buf);
		snprintf(bp, buf + sizeof buf - bp, " called %s", un);
	}
	if(dn) {
		bp = eos(buf);
		snprintf(bp, buf + sizeof buf - bp, " (%s)", dn);
	}
	return(buf);
}

char *
xname(struct obj *obj)
{
	char *buf = &(bufr[PREFIX]);	/* leave room for "17 -3 " */
	int nn = objects[obj->otyp].oc_name_known;
	char *an = objects[obj->otyp].oc_name;
	char *dn = objects[obj->otyp].oc_descr;
	char *un = objects[obj->otyp].oc_uname;
	int pl = (obj->quan != 1);
	size_t len = bufr + sizeof bufr - buf;

	if(!obj->dknown && !Blind) obj->dknown = 1; /* %% doesn't belong here */
	switch(obj->olet) {
	case AMULET_SYM:
		strlcpy(buf, (obj->spe < 0 && obj->known)
			? "cheap plastic imitation of the " : "", len);
		strlcat(buf,"Amulet of Yendor", len);
		break;
	case TOOL_SYM:
		if(!nn) {
			strlcpy(buf, dn, len);
			break;
		}
		strlcpy(buf,an,len);
		break;
	case FOOD_SYM:
		if(obj->otyp == DEAD_HOMUNCULUS && pl) {
			pl = 0;
			strlcpy(buf, "dead homunculi", len);
			break;
		}
		/* fungis ? */
		/* fall into next case */
	case WEAPON_SYM:
		if(obj->otyp == WORM_TOOTH && pl) {
			pl = 0;
			strlcpy(buf, "worm teeth", len);
			break;
		}
		if(obj->otyp == CRYSKNIFE && pl) {
			pl = 0;
			strlcpy(buf, "crysknives", len);
			break;
		}
		/* fall into next case */
	case ARMOR_SYM:
	case CHAIN_SYM:
	case ROCK_SYM:
		strlcpy(buf,an,len);
		break;
	case BALL_SYM:
		snprintf(buf, len, "%sheavy iron ball",
		  (obj->owt > objects[obj->otyp].oc_weight) ? "very " : "");
		break;
	case POTION_SYM:
		if(nn || un || !obj->dknown) {
			strlcpy(buf, "potion", len);
			if(pl) {
				pl = 0;
				strlcat(buf, "s", len);
			}
			if(!obj->dknown) break;
			if(un) {
				strlcat(buf, " called ", len);
				strlcat(buf, un, len);
			} else {
				strlcat(buf, " of ", len);
				strlcat(buf, an, len);
			}
		} else {
			strlcpy(buf, dn, len);
			strlcat(buf, " potion", len);
		}
		break;
	case SCROLL_SYM:
		strlcpy(buf, "scroll", len);
		if(pl) {
			pl = 0;
			strlcat(buf, "s", len);
		}
		if(!obj->dknown) break;
		if(nn) {
			strlcat(buf, " of ", len);
			strlcat(buf, an, len);
		} else if(un) {
			strlcat(buf, " called ", len);
			strlcat(buf, un, len);
		} else {
			strlcat(buf, " labeled ", len);
			strlcat(buf, dn, len);
		}
		break;
	case WAND_SYM:
		if(!obj->dknown)
			snprintf(buf, len, "wand");
		else if(nn)
			snprintf(buf, len, "wand of %s", an);
		else if(un)
			snprintf(buf, len, "wand called %s", un);
		else
			snprintf(buf, len, "%s wand", dn);
		break;
	case RING_SYM:
		if(!obj->dknown)
			snprintf(buf, len, "ring");
		else if(nn)
			snprintf(buf, len, "ring of %s", an);
		else if(un)
			snprintf(buf, len, "ring called %s", un);
		else
			snprintf(buf, len, "%s ring", dn);
		break;
	case GEM_SYM:
		if(!obj->dknown) {
			strlcpy(buf, "gem", len);
			break;
		}
		if(!nn) {
			snprintf(buf, len, "%s gem", dn);
			break;
		}
		strlcpy(buf, an, len);
		if(obj->otyp >= TURQUOISE && obj->otyp <= JADE)
			strlcat(buf, " stone", len);
		break;
	default:
		snprintf(buf,len,"glorkum %c (0%o) %u %d",
			obj->olet,obj->olet,obj->otyp,obj->spe);
	}
	if(pl) {
		char *p;

		for(p = buf; *p; p++) {
			if(!strncmp(" of ", p, 4)) {
				/* pieces of, cloves of, lumps of */
				int c1, c2 = 's';

				do {
					c1 = c2; c2 = *p; *p++ = c1;
				} while(c1);
				goto nopl;
			}
		}
		p = eos(buf)-1;
		if(*p == 's' || *p == 'z' || *p == 'x' ||
		    (*p == 'h' && p[-1] == 's'))
			strlcat(buf, "es", len);	/* boxes */
		else if(*p == 'y' && !strchr(vowels, p[-1]))
				/* rubies, zruties */
			strlcpy(p, "ies", bufr + sizeof bufr - p);
		else
			strlcat(buf, "s", len);
	}
nopl:
	if(obj->onamelth) {
		strlcat(buf, " named ", len);
		strlcat(buf, ONAME(obj), len);
	}
	return(buf);
}

char *
doname(struct obj *obj)
{
	char prefix[PREFIX];
	char *bp = xname(obj);
	char *p;

	if(obj->quan != 1)
		snprintf(prefix, sizeof prefix, "%u ", obj->quan);
	else
		strlcpy(prefix, "a ", sizeof prefix);
	switch(obj->olet) {
	case AMULET_SYM:
		if(strncmp(bp, "cheap ", 6))
			strlcpy(prefix, "the ", sizeof prefix);
		break;
	case ARMOR_SYM:
		if(obj->owornmask & W_ARMOR)
			strlcat(bp, " (being worn)", bufr + sizeof bufr - bp);
		/* fall into next case */
	case WEAPON_SYM:
		if(obj->known) {
			strlcat(prefix, sitoa(obj->spe), sizeof prefix);
			strlcat(prefix, " ", sizeof prefix);
		}
		break;
	case WAND_SYM:
		if(obj->known) {
			p = eos(bp);
			snprintf(p, bufr + sizeof bufr - p, " (%d)", obj->spe);
		}
		break;
	case RING_SYM:
		if(obj->owornmask & W_RINGR)
			strlcat(bp, " (on right hand)", bufr + sizeof bufr - bp);
		if(obj->owornmask & W_RINGL)
			strlcat(bp, " (on left hand)", bufr + sizeof bufr - bp);
		if(obj->known && (objects[obj->otyp].bits & SPEC)) {
			strlcat(prefix, sitoa(obj->spe), sizeof prefix);
			strlcat(prefix, " ", sizeof prefix);
		}
		break;
	}
	if(obj->owornmask & W_WEP)
		strlcat(bp, " (weapon in hand)", bufr + sizeof bufr - bp);
	if(obj->unpaid)
		strlcat(bp, " (unpaid)", bufr + sizeof bufr - bp);
	if(!strcmp(prefix, "a ") && strchr(vowels, *bp))
		strlcpy(prefix, "an ", sizeof prefix);
	bp = strprepend(bp, prefix);
	return(bp);
}

/* used only in hack.fight.c (thitu) */
void
setan(char *str, char *buf, size_t len)
{
	if(strchr(vowels,*str))
		snprintf(buf, len, "an %s", str);
	else
		snprintf(buf, len, "a %s", str);
}

char *
aobjnam(struct obj *otmp, char *verb)
{
	char *bp = xname(otmp);
	char prefix[PREFIX];

	if(otmp->quan != 1) {
		snprintf(prefix, sizeof prefix, "%u ", otmp->quan);
		bp = strprepend(bp, prefix);
	}

	if(verb) {
		/* verb is given in plural (i.e., without trailing s) */
		strlcat(bp, " ", bufr + sizeof bufr - bp);
		if(otmp->quan != 1)
			strlcat(bp, verb, bufr + sizeof bufr - bp);
		else if(!strcmp(verb, "are"))
			strlcat(bp, "is", bufr + sizeof bufr - bp);
		else {
			strlcat(bp, verb, bufr + sizeof bufr - bp);
			strlcat(bp, "s", bufr + sizeof bufr - bp);
		}
	}
	return(bp);
}

char *
Doname(struct obj *obj)
{
	char *s = doname(obj);

	if('a' <= *s && *s <= 'z') *s -= ('a' - 'A');
	return(s);
}

char *wrp[] = { "wand", "ring", "potion", "scroll", "gem" };
char wrpsym[] = { WAND_SYM, RING_SYM, POTION_SYM, SCROLL_SYM, GEM_SYM };

struct obj *
readobjnam(char *bp, size_t len)
{
	char *p, *cp = bp;
	int i;
	int cnt, spe, spesgn, typ, heavy;
	char let;
	char *un, *dn, *an;

/* int the = 0; char *oname = 0; */
	cnt = spe = spesgn = typ = heavy = 0;
	let = 0;
	an = dn = un = 0;
	for(p = cp; *p; p++)
		if('A' <= *p && *p <= 'Z') *p += 'a'-'A';
	if(!strncmp(cp, "the ", 4)){
/*		the = 1; */
		cp += 4;
	} else if(!strncmp(cp, "an ", 3)){
		cnt = 1;
		cp += 3;
	} else if(!strncmp(cp, "a ", 2)){
		cnt = 1;
		cp += 2;
	}
	if(!cnt && isdigit((unsigned char)*cp)){
		cnt = atoi(cp);
		while(isdigit((unsigned char)*cp)) cp++;
		while(*cp == ' ') cp++;
	}
	if(!cnt) cnt = 1;		/* %% what with "gems" etc. ? */

	if(*cp == '+' || *cp == '-'){
		spesgn = (*cp++ == '+') ? 1 : -1;
		spe = atoi(cp);
		while(isdigit((unsigned char)*cp)) cp++;
		while(*cp == ' ') cp++;
	} else {
		p = strrchr(cp, '(');
		if(p) {
			if(p > cp && p[-1] == ' ') p[-1] = 0;
			else *p = 0;
			p++;
			spe = atoi(p);
			while(isdigit((unsigned char)*p)) p++;
			if(strcmp(p, ")")) spe = 0;
			else spesgn = 1;
		}
	}
	/* now we have the actual name, as delivered by xname, say
		green potions called whisky
		scrolls labeled "QWERTY"
		egg
		dead zruties
		fortune cookies
		very heavy iron ball named hoei
		wand of wishing
		elven cloak
	*/
	for(p = cp; *p; p++) if(!strncmp(p, " named ", 7)) {
		*p = 0;
/*		oname = p+7; */
	}
	for(p = cp; *p; p++) if(!strncmp(p, " called ", 8)) {
		*p = 0;
		un = p+8;
	}
	for(p = cp; *p; p++) if(!strncmp(p, " labeled ", 9)) {
		*p = 0;
		dn = p+9;
	}

	/* first change to singular if necessary */
	if(cnt != 1) {
		/* find "cloves of garlic", "worthless pieces of blue glass" */
		for(p = cp; *p; p++) if(!strncmp(p, "s of ", 5)){
			while ((*p = p[1]))
				p++;
			goto sing;
		}
		/* remove -s or -es (boxes) or -ies (rubies, zruties) */
		p = eos(cp);
		if(p[-1] == 's') {
			if(p[-2] == 'e') {
				if(p[-3] == 'i') {
					if(!strcmp(p-7, "cookies"))
						goto mins;
					strlcpy(p-3, "y", bp + len - (p-3));
					goto sing;
				}

				/* note: cloves / knives from clove / knife */
				if(!strcmp(p-6, "knives")) {
					strlcpy(p-3, "fe", bp + len - (p-3));
					goto sing;
				}

				/* note: nurses, axes but boxes */
				if(!strcmp(p-5, "boxes")) {
					p[-2] = 0;
					goto sing;
				}
			}
		mins:
			p[-1] = 0;
		} else {
			if(!strcmp(p-9, "homunculi")) {
				strlcpy(p-1, "us", bp + len - (p-1));
				goto sing;
			}
			if(!strcmp(p-5, "teeth")) {
				strlcpy(p-5, "tooth", bp + len - (p-5));
				goto sing;
			}
			/* here we cannot find the plural suffix */
		}
	}
sing:
	if(!strcmp(cp, "amulet of yendor")) {
		typ = AMULET_OF_YENDOR;
		goto typfnd;
	}
	p = eos(cp);
	if(!strcmp(p-5, " mail")){	/* Note: ring mail is not a ring ! */
		let = ARMOR_SYM;
		an = cp;
		goto srch;
	}
	for(i = 0; i < sizeof(wrpsym); i++) {
		int j = strlen(wrp[i]);
		if(!strncmp(cp, wrp[i], j)){
			let = wrpsym[i];
			cp += j;
			if(!strncmp(cp, " of ", 4)) an = cp+4;
			/* else if(*cp) ?? */
			goto srch;
		}
		if(!strcmp(p-j, wrp[i])){
			let = wrpsym[i];
			p -= j;
			*p = 0;
			if(p[-1] == ' ') p[-1] = 0;
			dn = cp;
			goto srch;
		}
	}
	if(!strcmp(p-6, " stone")){
		p[-6] = 0;
		let = GEM_SYM;
		an = cp;
		goto srch;
	}
	if(!strcmp(cp, "very heavy iron ball")){
		heavy = 1;
		typ = HEAVY_IRON_BALL;
		goto typfnd;
	}
	an = cp;
srch:
	if(!an && !dn && !un)
		goto any;
	i = 1;
	if(let) i = bases[letindex(let)];
	while(i <= NROFOBJECTS && (!let || objects[i].oc_olet == let)){
		char *zn = objects[i].oc_name;

		if(!zn) goto nxti;
		if(an && strcmp(an, zn))
			goto nxti;
		if(dn && (!(zn = objects[i].oc_descr) || strcmp(dn, zn)))
			goto nxti;
		if(un && (!(zn = objects[i].oc_uname) || strcmp(un, zn)))
			goto nxti;
		typ = i;
		goto typfnd;
	nxti:
		i++;
	}
any:
	if(!let) let = wrpsym[rn2(sizeof(wrpsym))];
	typ = probtype(let);
typfnd:
	{ struct obj *otmp;
	let = objects[typ].oc_olet;
	otmp = mksobj(typ);
	if(heavy)
		otmp->owt += 15;
	if(cnt > 0 && strchr("%?!*)", let) &&
		(cnt < 4 || (let == WEAPON_SYM && typ <= ROCK && cnt < 20)))
		otmp->quan = cnt;

	if(spe > 3 && spe > otmp->spe)
		spe = 0;
	else if(let == WAND_SYM)
		spe = otmp->spe;
	if(spe == 3 && u.uluck < 0)
		spesgn = -1;
	if(let != WAND_SYM && spesgn == -1)
		spe = -spe;
	if(let == BALL_SYM)
		spe = 0;
	else if(let == AMULET_SYM)
		spe = -1;
	else if(typ == WAN_WISHING && rn2(10))
		spe = (rn2(10) ? -1 : 0);
	otmp->spe = spe;

	if(spesgn == -1)
		otmp->cursed = 1;

	return(otmp);
    }
}
