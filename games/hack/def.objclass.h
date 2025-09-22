/*	$OpenBSD: def.objclass.h,v 1.4 2003/03/16 21:22:35 camield Exp $*/
/*	$NetBSD: def.objclass.h,v 1.3 1995/03/23 08:29:34 cgd Exp $*/

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

/* definition of a class of objects */

struct objclass {
	char *oc_name;		/* actual name */
	char *oc_descr;		/* description when name unknown */
	char *oc_uname;		/* called by user */
	Bitfield(oc_name_known,1);
	Bitfield(oc_merge,1);	/* merge otherwise equal objects */
	char oc_olet;
	schar oc_prob;		/* probability for mkobj() */
	schar oc_delay;		/* delay when using such an object */
	uchar oc_weight;
	schar oc_oc1, oc_oc2;
	int oc_oi;
#define	nutrition	oc_oi	/* for foods */
#define	a_ac		oc_oc1	/* for armors - only used in ARM_BONUS */
#define ARM_BONUS(obj)	((10 - objects[obj->otyp].a_ac) + obj->spe)
#define	a_can		oc_oc2	/* for armors */
#define bits		oc_oc1	/* for wands and rings */
				/* wands */
#define		NODIR		1
#define		IMMEDIATE	2
#define		RAY		4
				/* rings */
#define		SPEC		1	/* +n is meaningful */
#define	wldam		oc_oc1	/* for weapons and PICK_AXE */
#define	wsdam		oc_oc2	/* for weapons and PICK_AXE */
#define	g_val		oc_oi	/* for gems: value on exit */
};

extern struct objclass objects[];

/* definitions of all object-symbols */

#define	ILLOBJ_SYM	'\\'
#define	AMULET_SYM	'"'
#define	FOOD_SYM	'%'
#define	WEAPON_SYM	')'
#define	TOOL_SYM	'('
#define	BALL_SYM	'0'
#define	CHAIN_SYM	'_'
#define	ROCK_SYM	'`'
#define	ARMOR_SYM	'['
#define	POTION_SYM	'!'
#define	SCROLL_SYM	'?'
#define	WAND_SYM	'/'
#define	RING_SYM	'='
#define	GEM_SYM		'*'
/* Other places with explicit knowledge of object symbols:
 * ....shk.c:	char shtypes[] = "=/)%?![";
 * mklev.c:	"=/)%?![<>"
 * hack.mkobj.c:	char mkobjstr[] = "))[[!!!!????%%%%/=**";
 * hack.apply.c:   otmp = getobj("0#%", "put in");
 * hack.eat.c:     otmp = getobj("%", "eat");
 * hack.invent.c:	   if(index("!%?[)=*(0/\"", sym)){
 * hack.invent.c:    || index("%?!*",otmp->olet))){
 */
