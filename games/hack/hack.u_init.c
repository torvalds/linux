/*	$OpenBSD: hack.u_init.c,v 1.11 2016/01/09 18:33:15 mestre Exp $	*/

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

#define	UNDEF_TYP	0
#define	UNDEF_SPE	'\177'
extern char plname[];

struct you zerou;
char pl_character[PL_CSIZ];
char *(roles[]) = {	/* must all have distinct first letter */
			/* roles[4] may be changed to -woman */
	"Tourist", "Speleologist", "Fighter", "Knight",
	"Cave-man", "Wizard"
};
#define	NR_OF_ROLES	SIZE(roles)
char rolesyms[NR_OF_ROLES + 1];		/* filled by u_init() */

struct trobj {
	uchar trotyp;
	schar trspe;
	char trolet;
	Bitfield(trquan,6);
	Bitfield(trknown,1);
};

#ifdef WIZARD
struct trobj Extra_objs[] = {
	{ 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0 }
};
#endif /* WIZARD */

struct trobj Cave_man[] = {
	{ MACE, 1, WEAPON_SYM, 1, 1 },
	{ BOW, 1, WEAPON_SYM, 1, 1 },
	{ ARROW, 0, WEAPON_SYM, 25, 1 },	/* quan is variable */
	{ LEATHER_ARMOR, 0, ARMOR_SYM, 1, 1 },
	{ 0, 0, 0, 0, 0}
};

struct trobj Fighter[] = {
	{ TWO_HANDED_SWORD, 0, WEAPON_SYM, 1, 1 },
	{ RING_MAIL, 0, ARMOR_SYM, 1, 1 },
	{ 0, 0, 0, 0, 0 }
};

struct trobj Knight[] = {
	{ LONG_SWORD, 0, WEAPON_SYM, 1, 1 },
	{ SPEAR, 2, WEAPON_SYM, 1, 1 },
	{ RING_MAIL, 1, ARMOR_SYM, 1, 1 },
	{ HELMET, 0, ARMOR_SYM, 1, 1 },
	{ SHIELD, 0, ARMOR_SYM, 1, 1 },
	{ PAIR_OF_GLOVES, 0, ARMOR_SYM, 1, 1 },
	{ 0, 0, 0, 0, 0 }
};

struct trobj Speleologist[] = {
	{ STUDDED_LEATHER_ARMOR, 0, ARMOR_SYM, 1, 1 },
	{ UNDEF_TYP, 0, POTION_SYM, 2, 0 },
	{ FOOD_RATION, 0, FOOD_SYM, 3, 1 },
	{ PICK_AXE, UNDEF_SPE, TOOL_SYM, 1, 0 },
	{ ICE_BOX, 0, TOOL_SYM, 1, 0 },
	{ 0, 0, 0, 0, 0}
};

struct trobj Tinopener[] = {
	{ CAN_OPENER, 0, TOOL_SYM, 1, 1 },
	{ 0, 0, 0, 0, 0 }
};

struct trobj Tourist[] = {
	{ UNDEF_TYP, 0, FOOD_SYM, 10, 1 },
	{ POT_EXTRA_HEALING, 0, POTION_SYM, 2, 0 },
	{ EXPENSIVE_CAMERA, 0, TOOL_SYM, 1, 1 },
	{ DART, 2, WEAPON_SYM, 25, 1 },	/* quan is variable */
	{ 0, 0, 0, 0, 0 }
};

struct trobj Wizard[] = {
	{ ELVEN_CLOAK, 0, ARMOR_SYM, 1, 1 },
	{ UNDEF_TYP, UNDEF_SPE, WAND_SYM, 2, 0 },
	{ UNDEF_TYP, UNDEF_SPE, RING_SYM, 2, 0 },
	{ UNDEF_TYP, UNDEF_SPE, POTION_SYM, 2, 0 },
	{ UNDEF_TYP, UNDEF_SPE, SCROLL_SYM, 3, 0 },
	{ 0, 0, 0, 0, 0 }
};

static void ini_inv(struct trobj *);
static int  role_index(char);
#ifdef WIZARD
static void wiz_inv(void);
#endif

void
u_init(void)
{
	int i;
	char exper = 'y', pc;

	if(flags.female)	/* should have been set in HACKOPTIONS */
		roles[4] = "Cave-woman";
	for(i = 0; i < NR_OF_ROLES; i++)
		rolesyms[i] = roles[i][0];
	rolesyms[i] = 0;

	if ((pc = pl_character[0])) {
		if (islower((unsigned char)pc))
			pc = toupper((unsigned char)pc);
		if ((i = role_index(pc)) >= 0)
			goto got_suffix;	/* implies experienced */
		printf("\nUnknown role: %c\n", pc);
		pl_character[0] = pc = 0;
	}

	printf("\nAre you an experienced player? [ny] ");

	while(!strchr("ynYN \n\004", (exper = readchar())))
		hackbell();
	if(exper == '\004')		/* Give him an opportunity to get out */
		end_of_input();
	printf("%c\n", exper);		/* echo */
	if(strchr("Nn \n", exper)) {
		exper = 0;
		goto beginner;
	}

	printf("\nTell me what kind of character you are:\n");
	printf("Are you");
	for(i = 0; i < NR_OF_ROLES; i++) {
		printf(" a %s", roles[i]);
		if(i == 2)			/* %% */
			printf(",\n\t");
		else if(i < NR_OF_ROLES - 2)
			printf(",");
		else if(i == NR_OF_ROLES - 2)
			printf(" or");
	}
	printf("? [%s] ", rolesyms);

	while ((pc = readchar())) {
		if(islower((unsigned char)pc))
			pc = toupper((unsigned char)pc);
		if((i = role_index(pc)) >= 0) {
			printf("%c\n", pc);	/* echo */
			(void) fflush(stdout);	/* should be seen */
			break;
		}
		if(pc == '\n')
			break;
		if(pc == '\004')    /* Give him the opportunity to get out */
			end_of_input();
		hackbell();
	}
	if(pc == '\n')
		pc = 0;

beginner:
	if(!pc) {
		printf("\nI'll choose a character for you.\n");
		i = rn2(NR_OF_ROLES);
		pc = rolesyms[i];
		printf("This game you will be a%s %s.\n",
			exper ? "n experienced" : "",
			roles[i]);
		getret();
		/* give him some feedback in case mklev takes much time */
		(void) putchar('\n');
		(void) fflush(stdout);
	}
#if 0
	/* Given the above code, I can't see why this would ever change
	   anything; it does core pretty well, though.  - cmh 4/20/93 */
	if(exper) {
		roles[i][0] = pc;
	}
#endif

got_suffix:

	(void) strlcpy(pl_character, roles[i], sizeof pl_character);
	flags.beginner = 1;
	u = zerou;
	u.usym = '@';
	u.ulevel = 1;
	init_uhunger();
#ifdef QUEST
	u.uhorizon = 6;
#endif /* QUEST */
	uarm = uarm2 = uarmh = uarms = uarmg = uwep = uball = uchain =
	uleft = uright = 0;

	switch(pc) {
	case 'c':
	case 'C':
		Cave_man[2].trquan = 12 + rnd(9)*rnd(9);
		u.uhp = u.uhpmax = 16;
		u.ustr = u.ustrmax = 18;
		ini_inv(Cave_man);
		break;
	case 't':
	case 'T':
		Tourist[3].trquan = 20 + rnd(20);
		u.ugold = u.ugold0 = rnd(1000);
		u.uhp = u.uhpmax = 10;
		u.ustr = u.ustrmax = 8;
		ini_inv(Tourist);
		if(!rn2(25)) ini_inv(Tinopener);
		break;
	case 'w':
	case 'W':
		for(i=1; i<=4; i++) if(!rn2(5))
			Wizard[i].trquan += rn2(3) - 1;
		u.uhp = u.uhpmax = 15;
		u.ustr = u.ustrmax = 16;
		ini_inv(Wizard);
		break;
	case 's':
	case 'S':
		Fast = INTRINSIC;
		Stealth = INTRINSIC;
		u.uhp = u.uhpmax = 12;
		u.ustr = u.ustrmax = 10;
		ini_inv(Speleologist);
		if(!rn2(10)) ini_inv(Tinopener);
		break;
	case 'k':
	case 'K':
		u.uhp = u.uhpmax = 12;
		u.ustr = u.ustrmax = 10;
		ini_inv(Knight);
		break;
	case 'f':
	case 'F':
		u.uhp = u.uhpmax = 14;
		u.ustr = u.ustrmax = 17;
		ini_inv(Fighter);
		break;
	default:	/* impossible */
		u.uhp = u.uhpmax = 12;
		u.ustr = u.ustrmax = 16;
	}
	find_ac();
	if(!rn2(20)) {
		int d = rn2(7) - 2;	/* biased variation */
		u.ustr += d;
		u.ustrmax += d;
	}

#ifdef WIZARD
	if(wizard) wiz_inv();
#endif /* WIZARD */

	/* make sure he can carry all he has - especially for T's */
	while(inv_weight() > 0 && u.ustr < 118)
		u.ustr++, u.ustrmax++;
}

static void
ini_inv(struct trobj *trop)
{
	struct obj *obj;

	while(trop->trolet) {
		obj = mkobj(trop->trolet);
		obj->known = trop->trknown;
		/* not obj->dknown = 1; - let him look at it at least once */
		obj->cursed = 0;
		if(obj->olet == WEAPON_SYM){
			obj->quan = trop->trquan;
			trop->trquan = 1;
		}
		if(trop->trspe != UNDEF_SPE)
			obj->spe = trop->trspe;
		if(trop->trotyp != UNDEF_TYP)
			obj->otyp = trop->trotyp;
		else
			if(obj->otyp == WAN_WISHING)	/* gitpyr!robert */
				obj->otyp = WAN_DEATH;
		obj->owt = weight(obj);	/* defined after setting otyp+quan */
		obj = addinv(obj);
		if(obj->olet == ARMOR_SYM){
			switch(obj->otyp){
			case SHIELD:
				if(!uarms) setworn(obj, W_ARMS);
				break;
			case HELMET:
				if(!uarmh) setworn(obj, W_ARMH);
				break;
			case PAIR_OF_GLOVES:
				if(!uarmg) setworn(obj, W_ARMG);
				break;
			case ELVEN_CLOAK:
				if(!uarm2)
					setworn(obj, W_ARM);
				break;
			default:
				if(!uarm) setworn(obj, W_ARM);
			}
		}
		if(obj->olet == WEAPON_SYM)
			if(!uwep) setuwep(obj);
#ifndef PYRAMID_BUG
		if(--trop->trquan) continue;	/* make a similar object */
#else
		if(trop->trquan) {		/* check if zero first */
			--trop->trquan;
			if(trop->trquan)
				continue;	/* make a similar object */
		}
#endif /* PYRAMID_BUG */
		trop++;
	}
}

#ifdef WIZARD
static void
wiz_inv(void)
{
	struct trobj *trop = &Extra_objs[0];
	char *ep = getenv("INVENT");
	int type;

	while(ep && *ep) {
		type = atoi(ep);
		ep = strchr(ep, ',');
		if(ep) while(*ep == ',' || *ep == ' ') ep++;
		if(type <= 0 || type > NROFOBJECTS) continue;
		trop->trotyp = type;
		trop->trolet = objects[type].oc_olet;
		trop->trspe = 4;
		trop->trknown = 1;
		trop->trquan = 1;
		ini_inv(trop);
	}
	/* give him a wand of wishing by default */
	trop->trotyp = WAN_WISHING;
	trop->trolet = WAND_SYM;
	trop->trspe = 20;
	trop->trknown = 1;
	trop->trquan = 1;
	ini_inv(trop);
}
#endif /* WIZARD */

void
plnamesuffix(void)
{
	char *p;

	if ((p = strrchr(plname, '-'))) {
		*p = 0;
		pl_character[0] = p[1];
		pl_character[1] = 0;
		if(!plname[0]) {
			askname();
			plnamesuffix();
		}
	}
}

/* must be called only from u_init() */
/* so that rolesyms[] is defined */
static int
role_index(char pc)
{		
	char *cp;

	if ((cp = strchr(rolesyms, pc)))
		return(cp - rolesyms);
	return(-1);
}
