/*	$OpenBSD: def.objects.h,v 1.5 2016/01/09 18:33:15 mestre Exp $*/
/*	$NetBSD: def.objects.h,v 1.3 1995/03/23 08:29:36 cgd Exp $*/

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

/* objects have letter " % ) ( 0 _ ` [ ! ? / = * */
struct objclass objects[] = {

	{ "strange object", NULL, NULL, 1, 0,
		ILLOBJ_SYM, 0, 0, 0, 0, 0, 0 },
	{ "amulet of Yendor", NULL, NULL, 1, 0,
		AMULET_SYM, 100, 0, 2, 0, 0, 0 },

#define	FOOD(name,prob,delay,weight,nutrition)	{ name, NULL, NULL, 1, 1,\
		FOOD_SYM, prob, delay, weight, 0, 0, nutrition }

/* dog eats foods 0-4 but prefers 1 above 0,2,3,4 */
/* food 4 can be read */
/* food 5 improves your vision */
/* food 6 makes you stronger (like Popeye) */
/* foods CORPSE up to CORPSE+52 are cadavers */

	FOOD("food ration", 	50, 5, 4, 800),
	FOOD("tripe ration",	20, 1, 2, 200),
	FOOD("pancake",		3, 1, 1, 200),
	FOOD("dead lizard",	3, 0, 1, 40),
	FOOD("fortune cookie",	7, 0, 1, 40),
	FOOD("carrot",		2, 0, 1, 50),
	FOOD("tin",		7, 0, 1, 0),
	FOOD("orange",		1, 0, 1, 80),
	FOOD("apple",		1, 0, 1, 50),
	FOOD("pear",		1, 0, 1, 50),
	FOOD("melon",		1, 0, 1, 100),
	FOOD("banana",		1, 0, 1, 80),
	FOOD("candy bar",	1, 0, 1, 100),
	FOOD("egg",		1, 0, 1, 80),
	FOOD("clove of garlic",	1, 0, 1, 40),
	FOOD("lump of royal jelly", 0, 0, 1, 200),

	FOOD("dead human",	0, 4, 40, 400),
	FOOD("dead giant ant",	0, 1, 3, 30),
	FOOD("dead giant bat",	0, 1, 3, 30),
	FOOD("dead centaur",	0, 5, 50, 500),
	FOOD("dead dragon",	0, 15, 150, 1500),
	FOOD("dead floating eye",	0, 1, 1, 10),
	FOOD("dead freezing sphere",	0, 1, 1, 10),
	FOOD("dead gnome",	0, 1, 10, 100),
	FOOD("dead hobgoblin",	0, 2, 20, 200),
	FOOD("dead stalker",	0, 4, 40, 400),
	FOOD("dead jackal",	0, 1, 10, 100),
	FOOD("dead kobold",	0, 1, 10, 100),
	FOOD("dead leprechaun",	0, 4, 40, 400),
	FOOD("dead mimic",	0, 4, 40, 400),
	FOOD("dead nymph",	0, 4, 40, 400),
	FOOD("dead orc",	0, 2, 20, 200),
	FOOD("dead purple worm",	0, 7, 70, 700),
	FOOD("dead quasit",	0, 2, 20, 200),
	FOOD("dead rust monster",	0, 5, 50, 500),
	FOOD("dead snake",	0, 1, 10, 100),
	FOOD("dead troll",	0, 4, 40, 400),
	FOOD("dead umber hulk",	0, 5, 50, 500),
	FOOD("dead vampire",	0, 4, 40, 400),
	FOOD("dead wraith",	0, 1, 1, 10),
	FOOD("dead xorn",	0, 7, 70, 700),
	FOOD("dead yeti",	0, 7, 70, 700),
	FOOD("dead zombie",	0, 1, 3, 30),
	FOOD("dead acid blob",	0, 1, 3, 30),
	FOOD("dead giant beetle",	0, 1, 1, 10),
	FOOD("dead cockatrice",	0, 1, 3, 30),
	FOOD("dead dog",	0, 2, 20, 200),
	FOOD("dead ettin",	0, 1, 3, 30),
	FOOD("dead fog cloud",	0, 1, 1, 10),
	FOOD("dead gelatinous cube",	0, 1, 10, 100),
	FOOD("dead homunculus",	0, 2, 20, 200),
	FOOD("dead imp",	0, 1, 1, 10),
	FOOD("dead jaguar",	0, 3, 30, 300),
	FOOD("dead killer bee",	0, 1, 1, 10),
	FOOD("dead leocrotta",	0, 5, 50, 500),
	FOOD("dead minotaur",	0, 7, 70, 700),
	FOOD("dead nurse",	0, 4, 40, 400),
	FOOD("dead owlbear",	0, 7, 70, 700),
	FOOD("dead piercer",	0, 2, 20, 200),
	FOOD("dead quivering blob",	0, 1, 10, 100),
	FOOD("dead giant rat",	0, 1, 3, 30),
	FOOD("dead giant scorpion",	0, 1, 10, 100),
	FOOD("dead tengu",	0, 3, 30, 300),
	FOOD("dead unicorn",	0, 3, 30, 300),
	FOOD("dead violet fungi",	0, 1, 10, 100),
	FOOD("dead long worm",	0, 5, 50, 500),
/* %% wt of long worm should be proportional to its length */
	FOOD("dead xan",	0, 3, 30, 300),
	FOOD("dead yellow light",	0, 1, 1, 10),
	FOOD("dead zruty",	0, 6, 60, 600),

/* weapons ... - ROCK come several at a time */
/* weapons ... - (ROCK-1) are shot using idem+(BOW-ARROW) */
/* weapons AXE, SWORD, THSWORD are good for worm-cutting */
/* weapons (PICK-)AXE, DAGGER, CRYSKNIFE are good for tin-opening */
#define WEAPON(name,prob,wt,ldam,sdam)	{ name, NULL, NULL, 1, 0 /*%%*/,\
		WEAPON_SYM, prob, 0, wt, ldam, sdam, 0 }

	WEAPON("arrow",		7, 0, 6, 6),
	WEAPON("sling bullet",	7, 0, 4, 6),
	WEAPON("crossbow bolt",	7, 0, 4, 6),
	WEAPON("dart",		7, 0, 3, 2),
	WEAPON("rock",		6, 1, 3, 3),
	WEAPON("boomerang",	2, 3, 9, 9),
	WEAPON("mace",		9, 3, 6, 7),
	WEAPON("axe",		6, 3, 6, 4),
	WEAPON("flail",		6, 3, 6, 5),
	WEAPON("long sword",	8, 3, 8, 12),
	WEAPON("two handed sword",	6, 4, 12, 6),
	WEAPON("dagger",	6, 3, 4, 3),
	WEAPON("worm tooth",	0, 4, 2, 2),
	WEAPON("crysknife",	0, 3, 10, 10),
	WEAPON("spear",		6, 3, 6, 8),
	WEAPON("bow",		6, 3, 4, 6),
	WEAPON("sling",		5, 3, 6, 6),
	WEAPON("crossbow",	6, 3, 4, 6),

	{ "whistle", "whistle", NULL, 0, 0,
		TOOL_SYM, 90, 0, 2, 0, 0, 0 },
	{ "magic whistle", "whistle", NULL, 0, 0,
		TOOL_SYM, 10, 0, 2, 0, 0, 0 },
	{ "expensive camera", NULL, NULL, 1, 1,
		TOOL_SYM, 0, 0, 3, 0, 0, 0 },
	{ "ice box", "large box", NULL, 0, 0,
		TOOL_SYM, 0, 0, 40, 0, 0, 0 },
	{ "pick-axe", NULL, NULL, 1, 1,
		TOOL_SYM, 0, 0, 5, 6, 3, 0 },
	{ "can opener", NULL, NULL, 1, 1,
		TOOL_SYM, 0, 0, 1, 0, 0, 0 },
	{ "heavy iron ball", NULL, NULL, 1, 0,
		BALL_SYM, 100, 0, 20, 0, 0, 0 },
	{ "iron chain", NULL, NULL, 1, 0,
		CHAIN_SYM, 100, 0, 20, 0, 0, 0 },
	{ "enormous rock", NULL, NULL, 1, 0,
		ROCK_SYM, 100, 0, 200 /* > MAX_CARR_CAP */, 0, 0, 0 },

#define ARMOR(name,prob,delay,ac,can)	{ name, NULL, NULL, 1, 0,\
		ARMOR_SYM, prob, delay, 8, ac, can, 0 }
	ARMOR("helmet",		 3, 1, 9, 0),
	ARMOR("plate mail",		 5, 5, 3, 2),
	ARMOR("splint mail",	 8, 5, 4, 1),
	ARMOR("banded mail",	10, 5, 4, 0),
	ARMOR("chain mail",		10, 5, 5, 1),
	ARMOR("scale mail",		10, 5, 6, 0),
	ARMOR("ring mail",		15, 5, 7, 0),
	/* the armors below do not rust */
	ARMOR("studded leather armor", 13, 3, 7, 1),
	ARMOR("leather armor",	17, 3, 8, 0),
	ARMOR("elven cloak",	 5, 0, 9, 3),
	ARMOR("shield",		 3, 0, 9, 0),
	ARMOR("pair of gloves",	 1, 1, 9, 0),

#define POTION(name,color)	{ name, color, NULL, 0, 1,\
		POTION_SYM, 0, 0, 2, 0, 0, 0 }

	POTION("restore strength",	"orange"),
	POTION("booze",		"bubbly"),
	POTION("invisibility",	"glowing"),
	POTION("fruit juice",	"smoky"),
	POTION("healing",	"pink"),
	POTION("paralysis",	"puce"),
	POTION("monster detection",	"purple"),
	POTION("object detection",	"yellow"),
	POTION("sickness",	"white"),
	POTION("confusion",	"swirly"),
	POTION("gain strength",	"purple-red"),
	POTION("speed",		"ruby"),
	POTION("blindness",	"dark green"),
	POTION("gain level",	"emerald"),
	POTION("extra healing",	"sky blue"),
	POTION("levitation",	"brown"),
	POTION(NULL,	"brilliant blue"),
	POTION(NULL,	"clear"),
	POTION(NULL,	"magenta"),
	POTION(NULL,	"ebony"),

#define SCROLL(name,text,prob) { name, text, NULL, 0, 1,\
		SCROLL_SYM, prob, 0, 3, 0, 0, 0 }
	SCROLL("mail",	"KIRJE", 0),
	SCROLL("enchant armor", "ZELGO MER", 6),
	SCROLL("destroy armor", "JUYED AWK YACC", 5),
	SCROLL("confuse monster", "NR 9", 5),
	SCROLL("scare monster", "XIXAXA XOXAXA XUXAXA", 4),
	SCROLL("blank paper", "READ ME", 3),
	SCROLL("remove curse", "PRATYAVAYAH", 6),
	SCROLL("enchant weapon", "DAIYEN FOOELS", 6),
	SCROLL("damage weapon", "HACKEM MUCHE", 5),
	SCROLL("create monster", "LEP GEX VEN ZEA", 5),
	SCROLL("taming", "PRIRUTSENIE", 1),
	SCROLL("genocide", "ELBIB YLOH",2),
	SCROLL("light", "VERR YED HORRE", 10),
	SCROLL("teleportation", "VENZAR BORGAVVE", 5),
	SCROLL("gold detection", "THARR", 4),
	SCROLL("food detection", "YUM YUM", 1),
	SCROLL("identify", "KERNOD WEL", 18),
	SCROLL("magic mapping", "ELAM EBOW", 5),
	SCROLL("amnesia", "DUAM XNAHT", 3),
	SCROLL("fire", "ANDOVA BEGARIN", 5),
	SCROLL("punishment", "VE FORBRYDERNE", 1),
	SCROLL(NULL, "VELOX NEB", 0),
	SCROLL(NULL, "FOOBIE BLETCH", 0),
	SCROLL(NULL, "TEMOV", 0),
	SCROLL(NULL, "GARVEN DEH", 0),

#define	WAND(name,metal,prob,flags)	{ name, metal, NULL, 0, 0,\
		WAND_SYM, prob, 0, 3, flags, 0, 0 }

	WAND("light",	"iridium",		10,	NODIR),
	WAND("secret door detection",	"tin",	5,	NODIR),
	WAND("create monster",	"platinum",	5,	NODIR),
	WAND("wishing",		"glass",	1,	NODIR),
	WAND("striking",	"zinc",		9,	IMMEDIATE),
	WAND("slow monster",	"balsa",	5,	IMMEDIATE),
	WAND("speed monster",	"copper",	5,	IMMEDIATE),
	WAND("undead turning",	"silver",	5,	IMMEDIATE),
	WAND("polymorph",	"brass",	5,	IMMEDIATE),
	WAND("cancellation",	"maple",	5,	IMMEDIATE),
	WAND("teleportation",	"pine",		5,	IMMEDIATE),
	WAND("make invisible",	"marble",	9,	IMMEDIATE),
	WAND("digging",		"iron",		5,	RAY),
	WAND("magic missile",	"aluminium",	10,	RAY),
	WAND("fire",	"steel",	5,	RAY),
	WAND("sleep",	"curved",	5,	RAY),
	WAND("cold",	"short",	5,	RAY),
	WAND("death",	"long",		1,	RAY),
	WAND(NULL,	"oak",		0,	0),
	WAND(NULL,	"ebony",	0,	0),
	WAND(NULL,	"runed",	0,	0),

#define	RING(name,stone,spec)	{ name, stone, NULL, 0, 0,\
		RING_SYM, 0, 0, 1, spec, 0, 0 }

	RING("adornment",	"engagement",	0),
	RING("teleportation",	"wooden",	0),
	RING("regeneration",	"black onyx",	0),
	RING("searching",	"topaz",	0),
	RING("see invisible",	"pearl",	0),
	RING("stealth",		"sapphire",	0),
	RING("levitation",	"moonstone",	0),
	RING("poison resistance", "agate",	0),
	RING("aggravate monster", "tiger eye",	0),
	RING("hunger",		"shining",	0),
	RING("fire resistance",	"gold",		0),
	RING("cold resistance",	"copper",	0),
	RING("protection from shape changers", "diamond", 0),
	RING("conflict",	"jade",		0),
	RING("gain strength",	"ruby",		SPEC),
	RING("increase damage",	"silver",	SPEC),
	RING("protection",	"granite",	SPEC),
	RING("warning",		"wire",		0),
	RING("teleport control", "iron",	0),
	RING(NULL,		"ivory",	0),
	RING(NULL,		"blackened",	0),

/* gems ************************************************************/
#define	GEM(name,color,prob,gval)	{ name, color, NULL, 0, 1,\
		GEM_SYM, prob, 0, 1, 0, 0, gval }
	GEM("diamond", "blue", 1, 4000),
	GEM("ruby", "red", 1, 3500),
	GEM("sapphire", "blue", 1, 3000),
	GEM("emerald", "green", 1, 2500),
	GEM("turquoise", "green", 1, 2000),
	GEM("aquamarine", "blue", 1, 1500),
	GEM("tourmaline", "green", 1, 1000),
	GEM("topaz", "yellow", 1, 900),
	GEM("opal", "yellow", 1, 800),
	GEM("garnet", "dark", 1, 700),
	GEM("amethyst", "violet", 2, 650),
	GEM("agate", "green", 2, 600),
	GEM("onyx", "white", 2, 550),
	GEM("jasper", "yellowish brown", 2, 500),
	GEM("jade", "green", 2, 450),
	GEM("worthless piece of blue glass", "blue", 20, 0),
	GEM("worthless piece of red glass", "red", 20, 0),
	GEM("worthless piece of yellow glass", "yellow", 20, 0),
	GEM("worthless piece of green glass", "green", 20, 0),
	{ NULL, NULL, NULL, 0, 0, ILLOBJ_SYM, 0, 0, 0, 0, 0, 0 }
};

char obj_symbols[] = {
	ILLOBJ_SYM, AMULET_SYM, FOOD_SYM, WEAPON_SYM, TOOL_SYM,
	BALL_SYM, CHAIN_SYM, ROCK_SYM, ARMOR_SYM, POTION_SYM, SCROLL_SYM,
	WAND_SYM, RING_SYM, GEM_SYM, 0 };
int bases[sizeof(obj_symbols)];
