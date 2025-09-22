/*	$OpenBSD: extern.h,v 1.23 2022/08/08 17:57:05 op Exp $	*/
/*	$NetBSD: extern.h,v 1.5 1995/04/24 12:22:18 cgd Exp $	*/

/*
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
 *
 *	@(#)extern.h	8.1 (Berkeley) 5/31/93
 */

#include <sys/types.h>

#define BITS (8 * sizeof (int))

#define OUTSIDE		(position > 68 && position < 246 && position != 218)
#define rnd(x)		arc4random_uniform(x)
#define max(a,b)	((a) < (b) ? (b) : (a))
#define TestBit(array, index)	(array[index/BITS] & (1U << (index % BITS)))
#define SetBit(array, index)	(array[index/BITS] |= (1U << (index % BITS)))
#define ClearBit(array, index)	(array[index/BITS] &= ~(1U << (index % BITS)))
/*
 * These macros yield words to use with objects (followed but not preceded
 * by spaces, or with no spaces if the expansion is the empty string).
 */
#define A_OR_AN(n)		(objflags[(n)] & OBJ_AN ? "an " : "a ")
#define IS_PLURAL(n)	(objflags[(n)] & OBJ_PLURAL)
#define A_OR_AN_OR_THE(n)	(IS_PLURAL((n)) ? "the " : A_OR_AN((n)))
#define A_OR_AN_OR_BLANK(n)	(IS_PLURAL((n)) ? "" : A_OR_AN((n)))
#define IS_OR_ARE(n)		(IS_PLURAL((n)) ? "are " : "is ")

 /* well known rooms */
#define FINAL	275
#define GARDEN	197
#define POOLS	126
#define DOCK	93

 /* word types */
#define VERB	0
#define OBJECT  1
#define NOUNS	2
#define PREPS	3
#define ADJS	4
#define CONJ	5

 /* words numbers */
#define KNIFE		0
#define SWORD		1
#define LAND		2
#define WOODSMAN 	3
#define TWO_HANDED	4
#define CLEAVER		5
#define BROAD		6
#define MAIL		7
#define HELM		8
#define SHIELD		9
#define MAID		10
#define BODY		10
#define VIPER		11
#define LAMPON		12
#define SHOES		13
#define CYLON		14
#define PAJAMAS		15
#define ROBE		16
#define AMULET		17
#define MEDALION	18
#define TALISMAN	19
#define DEADWOOD	20
#define MALLET		21
#define LASER		22
#define BATHGOD		23
#define NORMGOD		24
#define GRENADE		25
#define CHAIN		26
#define ROPE		27
#define LEVIS		28
#define MACE		29
#define SHOVEL		30
#define HALBERD		31
#define COMPASS		32
#define CRASH		33
#define ELF		34
#define FOOT		35
#define COINS		36
#define MATCHES		37
#define MAN		38
#define PAPAYAS		39
#define PINEAPPLE	40
#define KIWI		41
#define COCONUTS	42
#define MANGO		43
#define RING		44
#define POTION		45
#define BRACELET	46
#define GIRL		47
#define GIRLTALK	48
#define DARK		49
#define TIMER		50
#define CHAR		53
#define BOMB		54
#define DEADGOD		55
#define DEADTIME	56
#define DEADNATIVE	57
#define NATIVE		58
#define HORSE		59
#define CAR		60
#define POT		61
#define BAR		62
#define BLOCK		63
#define NUMOFOBJECTS	64
 /* non-objects below */
#define UP	1000
#define DOWN	1001
#define AHEAD	1002
#define BACK	1003
#define RIGHT	1004
#define LEFT	1005
#define TAKE	1006
#define USE	1007
#define LOOK	1008
#define QUIT	1009
#define NORTH	1010
#define SOUTH	1011
#define EAST	1012
#define WEST	1013
#define SU	1014
#define DROP	1015
#define TAKEOFF	1016
#define DRAW	1017
#define PUTON	1018
#define WEARIT	1019
#define PUT	1020
#define INVEN	1021
#define EVERYTHING 1022
#define AND	1023
#define KILL	1024
#define RAVAGE	1025
#define UNDRESS	1026
#define THROW	1027
#define LAUNCH	1028
#define LANDIT	1029
#define LIGHT	1030
#define FOLLOW	1031
#define KISS	1032
#define LOVE	1033
#define GIVE	1034
#define SMITE	1035
#define SHOOT	1036
#define ON	1037
#define OFF	1038
#define TIME	1039
#define SLEEP	1040
#define DIG	1041
#define EAT	1042
#define SWIM	1043
#define DRINK	1044
#define DOOR	1045
#define SAVE	1046
#define RIDE	1047
#define DRIVE	1048
#define SCORE	1049
#define BURY	1050
#define JUMP	1051
#define KICK	1052
#define OPEN	1053
#define VERBOSE	1054
#define BRIEF	1055
#define AUXVERB 1056

 /* injuries */
#define ARM	6		/* broken arm */
#define RIBS	7		/* broken ribs */
#define SPINE	9		/* broken back */
#define SKULL	11		/* fractured skull */
#define INCISE	10		/* deep incisions */
#define NECK	12		/* broken NECK */
#define NUMOFINJURIES 13

 /* notes */
#define CANTLAUNCH	0
#define LAUNCHED	1
#define CANTSEE		2
#define CANTMOVE	3
#define JINXED		4
#define DUG		5
#define NUMOFNOTES	6

 /* number of times room description shown */
#define ROOMDESC	3

 /* fundamental constants */
#define NUMOFROOMS	275
#define NUMOFWORDS	((NUMOFOBJECTS + BITS - 1) / BITS)
#define LINELENGTH	81

#define TODAY		0
#define TONIGHT		1
#define CYCLE		100

 /* initial variable values */
#define TANKFULL	250
#define TORPEDOES	10
#define MAXWEIGHT	60
#define MAXCUMBER	10

/* Flags for objects */
#define OBJ_PLURAL	1
#define OBJ_AN		2
#define OBJ_PERSON	4
#define OBJ_NONOBJ	8	/* footsteps, asteroids, etc. */

struct room {
	const char   *name;
	int     link[8];
#define north	link[0]
#define south	link[1]
#define east	link[2]
#define west	link[3]
#define up	link[4]
#define access	link[5]
#define down	link[6]
#define flyhere	link[7]
	const char   *desc;
	unsigned int objects[NUMOFWORDS];
};
extern struct room dayfile[];
extern struct room nightfile[];
extern struct room *location;

 /* object characteristics */
extern const char   *const objdes[NUMOFOBJECTS];
extern const char   *const objsht[NUMOFOBJECTS];
extern const char   *const ouch[NUMOFINJURIES];
extern const int     objwt[NUMOFOBJECTS];
extern const int     objcumber[NUMOFOBJECTS];
extern const int     objflags[NUMOFOBJECTS];

 /* current input line */
#define WORDLEN 15
#define NWORD	20		/* words per line */
extern char    words[NWORD][WORDLEN];
extern int     wordvalue[NWORD];
extern int     wordtype[NWORD];
extern int     wordcount, wordnumber;
extern int     stop_cypher;	/* continue parsing the current line? */

 /* state of the game */
extern int     ourtime;
extern int     position;
extern int     direction;
extern int     left, right, ahead, back;
extern int     ourclock, fuel, torps;
extern int     carrying, encumber;
extern int     rythmn;
extern int     followfight;
extern int     ate;
extern int     snooze;
extern int     meetgirl;
extern int     followgod;
extern int     godready;
extern int     win;
extern int     wintime;
extern int     tempwiz;
extern int     matchlight;
extern int     matchcount;
extern int     loved;
extern int     pleasure, power, ego;
extern int     WEIGHT;
extern int     CUMBER;
extern int     notes[NUMOFNOTES];
extern unsigned int inven[NUMOFWORDS];
extern unsigned int wear[NUMOFWORDS];
extern char    beenthere[NUMOFROOMS+1];
extern char    injuries[NUMOFINJURIES];
extern int     verbose;

extern const char *username;

struct wlist {
	const char   *string;
	int     value, article;
	struct wlist *next;
};
extern struct wlist wlist[];

struct objs {
	short room;
	short obj;
};
extern const struct objs dayobjs[];
extern const struct objs nightobjs[];

void bury(void);
int card(const char *, int);
void chime(void);
void convert(int);
void crash(void);
int cypher(void);
__dead void die(int);
void dig(void);
void dooropen(void);
int draw(void);
void drink(void);
int drive(void);
int drop(const char *);
int eat(void);
int fight(int, int);
int follow(void);
char *getcom(char *, int, const char *, const char *);
char *getword(char *, char *, int);
int give(void);
int inc_wordnumber(const char *, const char *);
void initialize(const char *);
int jump(void);
void kiss(void);
int land(void);
int launch(void);
void light(void);
__dead void live(void);
void love(void);
int moveplayer(int, int);
void murder(void);
void newlocation(void);
void news(void);
void newway(int);
void open_score_file(void);
void parse(void);
void post(char);
void printobjs(void);
int put(void);
int puton(void);
const char *rate(void);
void ravage(void);
void restore(const char *);
int ride(void);
void save(const char *);
char *save_file_name(const char *);
int shoot(void);
int take(unsigned int[]);
int takeoff(void);
int throw(const char *);
const char *truedirec(int, char);
int ucard(const unsigned int *);
void undress(void);
int use(void);
int visual(void);
int wearit(void);
void whichway(struct room);
void wordinit(void);
void writedes(void);
int zzz(void);
