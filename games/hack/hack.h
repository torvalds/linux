/*	$OpenBSD: hack.h,v 1.15 2024/05/21 05:00:47 jsg Exp $*/
/*	$NetBSD: hack.h,v 1.3 1995/03/23 08:30:21 cgd Exp $*/

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

#include <fcntl.h>
#include <stdarg.h>
#include <string.h>

#define	Null(type)	((struct type *) 0)

#include "config.h"
#include "def.objclass.h"

typedef struct {
	xchar x,y;
} coord;

#include "def.mkroom.h"
#include "def.monst.h"	/* uses coord */
#include "def.gold.h"
#include "def.trap.h"
#include "def.obj.h"
#include "def.flag.h"
#include "def.wseg.h"

#define	plur(x)	(((x) == 1) ? "" : "s")

#define	BUFSZ	256	/* for getlin buffers */
#define	PL_NSIZ	32	/* name of player, ghost, shopkeeper */

#include "def.rm.h"
#include "def.permonst.h"

extern xchar xdnstair, ydnstair, xupstair, yupstair; /* stairs up and down. */

extern xchar dlevel;
#define	newstring(x)	(char *) alloc((unsigned)(x))

#include "hack.onames.h"

#define ON 1
#define OFF 0

extern struct obj *invent, *uwep, *uarm, *uarm2, *uarmh, *uarms, *uarmg, 
	*uleft, *uright, *fcobj;
extern struct obj *uchain;	/* defined iff PUNISHED */
extern struct obj *uball;	/* defined if PUNISHED */

struct prop {
#define	TIMEOUT		007777	/* mask */
#define	LEFT_RING	W_RINGL	/* 010000L */
#define	RIGHT_RING	W_RINGR	/* 020000L */
#define	INTRINSIC	040000L
#define	LEFT_SIDE	LEFT_RING
#define	RIGHT_SIDE	RIGHT_RING
#define	BOTH_SIDES	(LEFT_SIDE | RIGHT_SIDE)
	long p_flgs;
	int (*p_tofn)();	/* called after timeout */
};

struct you {
	xchar ux, uy;
	schar dx, dy, dz;	/* direction of move (or zap or ... ) */
#ifdef QUEST
	schar di;		/* direction of FF */
	xchar ux0, uy0;		/* initial position FF */
#endif /* QUEST */
	xchar udisx, udisy;	/* last display pos */
	char usym;		/* usually '@' */
	schar uluck;
#define	LUCKMAX		10	/* on moonlit nights 11 */
#define	LUCKMIN		(-10)
	int last_str_turn:3;	/* 0: none, 1: half turn, 2: full turn */
				/* +: turn right, -: turn left */
	unsigned udispl:1;	/* @ on display */
	unsigned ulevel:4;	/* 1 - 14 */
#ifdef QUEST
	unsigned uhorizon:7;
#endif /* QUEST */
	unsigned utrap:3;	/* trap timeout */
	unsigned utraptype:1;	/* defined if utrap nonzero */
#define	TT_BEARTRAP	0
#define	TT_PIT		1
	unsigned uinshop:6;	/* used only in shk.c - (roomno+1) of shop */


/* perhaps these #define's should also be generated by makedefs */
#define	TELEPAT		LAST_RING		/* not a ring */
#define	Telepat		u.uprops[TELEPAT].p_flgs
#define	FAST		(LAST_RING+1)		/* not a ring */
#define	Fast		u.uprops[FAST].p_flgs
#define	CONFUSION	(LAST_RING+2)		/* not a ring */
#define	Confusion	u.uprops[CONFUSION].p_flgs
#define	INVIS		(LAST_RING+3)		/* not a ring */
#define	Invis		u.uprops[INVIS].p_flgs
#define Invisible	(Invis && !See_invisible)
#define	GLIB		(LAST_RING+4)		/* not a ring */
#define	Glib		u.uprops[GLIB].p_flgs
#define	PUNISHED	(LAST_RING+5)		/* not a ring */
#define	Punished	u.uprops[PUNISHED].p_flgs
#define	SICK		(LAST_RING+6)		/* not a ring */
#define	Sick		u.uprops[SICK].p_flgs
#define	BLIND		(LAST_RING+7)		/* not a ring */
#define	Blind		u.uprops[BLIND].p_flgs
#define	WOUNDED_LEGS	(LAST_RING+8)		/* not a ring */
#define Wounded_legs	u.uprops[WOUNDED_LEGS].p_flgs
#define STONED		(LAST_RING+9)		/* not a ring */
#define Stoned		u.uprops[STONED].p_flgs
#define PROP(x) (x-RIN_ADORNMENT)       /* convert ring to index in uprops */
	unsigned umconf:1;
	char *usick_cause;
	struct prop uprops[LAST_RING+10];

	unsigned uswallow:1;		/* set if swallowed by a monster */
	unsigned uswldtim:4;		/* time you have been swallowed */
	unsigned uhs:3;			/* hunger state - see hack.eat.c */
	schar ustr,ustrmax;
	schar udaminc;
	schar uac;
	int uhp,uhpmax;
	long int ugold,ugold0,uexp,urexp;
	int uhunger;			/* refd only in eat.c and shk.c */
	int uinvault;
	struct monst *ustuck;
	int nr_killed[CMNUM+2];		/* used for experience bookkeeping */
};

extern struct you u;

extern char *traps[];
extern char vowels[];

extern xchar curx,cury;	/* cursor location on screen */

extern coord bhitpos;	/* place where thrown weapon falls to the ground */

extern xchar seehx,seelx,seehy,seely; /* where to see*/
extern char *save_cm,*killer;

extern xchar dlevel, maxdlevel; /* dungeon level */

extern long moves;

extern int multi;


extern char lock[PL_NSIZ+4];


#define DIST(x1,y1,x2,y2)       (((x1)-(x2))*((x1)-(x2)) + ((y1)-(y2))*((y1)-(y2)))

#define	PL_CSIZ		20	/* sizeof pl_character */
#define	MAX_CARR_CAP	120	/* so that boulders can be heavier */
#define	MAXLEVEL	40
#define	FAR	(COLNO+2)	/* position outside screen */


/* alloc.c */
void *alloc(unsigned int);

/* hack.apply.c */
int  doapply(void);
int  holetime(void);
void dighole(void);

/* hack.bones.c */
void savebones(void);
int  getbones(void);

/* hack.c */
void unsee(void);
void seeoff(int);
void domove(void);
int  dopickup(void);
void pickup(int);
void lookaround(void);
int  monster_nearby(void);
int  cansee(xchar, xchar);
int  sgn(int);
void setsee(void);
void nomul(int);
int  abon(void);
int  dbon(void);
void losestr(int);
void losehp(int, char *);
void losehp_m(int, struct monst *);
void losexp(void);
int  inv_weight(void);
long newuexp(void);

/* hack.cmd.c */
void rhack(char *);
int  doextcmd(void);
int  movecmd(char);
int  getdir(boolean);
void confdir(void);
#ifdef QUEST
void finddir(void);
int  isroom(int, int);
#endif
int  isok(int, int);

/* hack.do.c */
int  dodrop(void);
void dropx(struct obj *);
void dropy(struct obj *);
int  doddrop(void);
int  dodown(void);
int  doup(void);
void goto_level(int, boolean);
int  donull(void);
int  dopray(void);
int  dothrow(void);
struct obj *splitobj(struct obj *, int);
void more_experienced(int, int);
void set_wounded_legs(long, int);
void heal_legs(void);

/* hack.do_name.c */
coord getpos(int, char *);
int  do_mname(void);
int  ddocall(void);
void docall(struct obj *);
char *xmonnam(struct monst *, int);
char *monnam(struct monst *);
char *Monnam(struct monst *);
char *amonnam(struct monst *, char *);
char *Amonnam(struct monst *, char *);
char *Xmonnam(struct monst *);

/* hack.do_wear.c */
int  doremarm(void);
int  doremring(void);
int  armoroff(struct obj *);
int  doweararm(void);
int  dowearring(void);
void ringoff(struct obj *);
void find_ac(void);
void glibr(void);
struct obj *some_armor(void);
void corrode_armor(void);

/* hack.dog.c */
void makedog(void);
void losedogs(void);
void keepdogs(void);
void fall_down(struct monst *);
int  dog_move(struct monst *, int);
int  inroom(xchar, xchar);
int  tamedog(struct monst *, struct obj *);

/* hack.eat.c */
void init_uhunger(void);
int  opentin(void);
void Meatdone(void);
int  doeat(void);
void gethungry(void);
void morehungry(int);
void lesshungry(int);
void unfaint(void);
int  poisonous(struct obj *);

/* hack.end.c */
void done1(int);
int  done2(void);
void done_intr(int);
void done_hangup(int);
void done_in_by(struct monst *);
void done(char *);
void clearlocks(void);
char *eos(char *);
void charcat(char *, char);
void prscore(int, char **);

/* hack.engrave.c */
struct engr *engr_at(xchar, xchar);
int  sengr_at(char *, xchar, xchar);
void u_wipe_engr(int);
void wipe_engr_at(xchar, xchar, xchar);
void read_engr_at(int, int);
void make_engr_at(int, int, char *);
int  doengrave(void);
void save_engravings(int);
void rest_engravings(int);

/* hack.fight.c */
int  hitmm(struct monst *, struct monst *);
void mondied(struct monst *);
int  fightm(struct monst *);
int  thitu(int, int, char *);
boolean hmon(struct monst *, struct obj *, int);
boolean attack(struct monst *);

/* hack.invent.c */
struct obj *addinv(struct obj *);
void useup(struct obj *);
void freeinv(struct obj *);
void delobj(struct obj *);
void freeobj(struct obj *);
void freegold(struct gold *);
void deltrap(struct trap *);
struct monst *m_at(int, int);
struct obj   *o_at(int, int);
struct obj   *sobj_at(int, int, int);
int  carried(struct obj *);
boolean carrying(int);
struct obj *o_on(unsigned int, struct obj *);
struct trap *t_at(int, int);
struct gold *g_at(int, int);
struct obj *mkgoldobj(long);
struct obj *getobj(char *, char *);
int  ckunpaid(struct obj *);
int  ggetobj(char *, int (*fn)(struct obj *), int);
int  askchain(struct obj *, char *, int, int (*fn)(struct obj *),
    int (*ckfn)(struct obj *), int);
void prinv(struct obj *);
int  ddoinv(void);
int  dotypeinv(void);
int  dolook(void);
void stackobj(struct obj *);
int  countgold(void);
int  doprgold(void);
int  doprwep(void);
int  doprarm(void);
int  doprring(void);

/* hack.ioctl.c */
void getioctls(void);
void setioctls(void);
#ifdef SUSPEND
int  dosuspend(void);
#endif

/* hack.lev.c */
void savelev(int, xchar);
void bwrite(int, const void *, ssize_t);
void saveobjchn(int, struct obj *);
void savemonchn(int, struct monst *);
void savegoldchn(int, struct gold *);
void savetrapchn(int, struct trap *);
void getlev(int, int, xchar);
void mread(int, char *, unsigned);
void mklev(void);

/* hack.main.c */
void glo(int);
void askname(void);
void impossible(const char *, ...) __attribute__((__format__ (printf, 1, 2)));
/* ... stuff: fix in files; printf-like ones have spec _attrib or
 * something */
void stop_occupation(void);

/* hack.makemon.c */
struct monst *makemon(struct permonst *, int, int);
coord enexto(xchar, xchar);
int  goodpos(int, int);
void rloc(struct monst *);
struct monst *mkmon_at(char, int, int);

/* hack.mhitu.c */
int  mhitu(struct monst *);
int  hitu(struct monst *, int);

/* hack.mklev.c */
void makelevel(void);
int  makerooms(void);
void mktrap(int, int, struct mkroom *);

/* hack.mkmaze.c */
void makemaz(void);
coord mazexy(void);

/* hack/mkobj.c */
struct obj * mkobj_at(int, int, int);
void mksobj_at(int, int, int);
struct obj *mkobj(int);
struct obj *mksobj(int);
int  letter(int);
int  weight(struct obj *);
void mkgold(long, int, int);

/* hack.mkshop.c */
#ifndef QUEST
void mkshop(void);
void mkzoo(int);
struct permonst *morguemon(void);
void mkswamp(void);
#endif

/* hack.mon.c */
void movemon(void);
void justswld(struct monst *, char *);
void youswld(struct monst *, int, int, char *);
int  dochug(struct monst *);
int  m_move(struct monst *, int);
int  mfndpos(struct monst *, coord pos[9], int info[9], int);
int  dist(int, int);
void poisoned(char *, char *);
void mondead(struct monst *);
void replmon(struct monst *, struct monst *);
void relmon(struct monst *);
void monfree(struct monst *);
void unstuck(struct monst *);
void killed(struct monst *);
void kludge(char *, char *);
void rescham(void);
int  newcham(struct monst *, struct permonst *);
void mnexto(struct monst *);
void setmangry(struct monst *);
int  canseemon(struct monst *);

/* hack.o_init.c */
int  letindex(char);
void init_objects(void);
int  probtype(char);
void oinit(void);
void savenames(int);
void restnames(int);
int  dodiscovered(void);

/* hack.objnam.c */
char *strprepend(char *, char *);
char *typename(int);
char *xname(struct obj *);
char *doname(struct obj *);
void setan(char *, char *, size_t);
char *aobjnam(struct obj *, char *);
char *Doname(struct obj *);
struct obj *readobjnam(char *, size_t);

/* hack.options.c */
void initoptions(void);
int  doset(void);

/* hack.pager.c */
int  dowhatis(void);
void intruph(int);
void set_whole_screen(void);
#ifdef NEWS
int  readnews(void);
#endif
void set_pager(int);
int  page_line(char *);
void cornline(int, char *);
int  dohelp(void);
int  page_file(char *, boolean);
#ifdef UNIX
#ifdef SHELL
int  dosh(void);
#endif
int  child(int);
#endif

/* hack.potion.c */
int  dodrink(void);
void pluslvl(void);
void strange_feeling(struct obj *, char *);
void potionhit(struct monst *, struct obj *);
void potionbreathe(struct obj *);
int  dodip(void);

/* hack.pri.c */
void swallowed(void);
void panic(const char *, ...) __attribute__((__format__ (printf, 1, 2)));
void atl(int, int, int);
void on_scr(int, int);
void tmp_at(schar, schar);
void Tmp_at(schar, schar);
void setclipped(void);
void at(xchar, xchar, char);
void prme(void);
int  doredraw(void);
void docrt(void);
void docorner(int, int);
void curs_on_u(void);
void pru(void);
void prl(int, int);
char news0(xchar, xchar);
void newsym(int, int);
void mnewsym(int, int);
void nosee(int, int);
#ifndef QUEST
void prl1(int, int);
void nose1(int, int);
#endif
int  vism_at(int, int);
void unpobj(struct obj *);
void seeobjs(void);
void seemons(void);
void pmon(struct monst *);
void unpmon(struct monst *);
void nscr(void);
void bot(void);
void cls(void);

/* hack.read.c */
int  doread(void);
int  identify(struct obj *);
void litroom(boolean);

/* hack.rip.c */
void outrip(void);

/* hack.rumors.c */
void outrumor(void);

/* hack.save.c */
int  dosave(void);
__dead void hackhangup(int);
int  dorecover(int);
struct obj *restobjchn(int);
struct monst *restmonchn(int);

/* hack.search.c */
int  findit(void);
int  dosearch(void);
int  doidtrap(void);
void wakeup(struct monst *);
void seemimic(struct monst *);

/* hack.shk.c */
char *shkname(struct monst *);
void shkdead(struct monst *);
void replshk(struct monst *, struct monst *);
int  inshop(void);
void obfree(struct obj *, struct obj *);
int  dopay(void);
void paybill(void);
void addtobill(struct obj *);
void splitbill(struct obj *, struct obj *);
void subfrombill(struct obj *);
int  doinvbill(int);
int  shkcatch(struct obj *);
int  shk_move(struct monst *);
void shopdig(int);
int  online(int, int);
int  follower(struct monst *);

/* hack.shknam.c */
void findname(char *, size_t, char);

/* hack.steal.c */
long somegold(void);
void stealgold(struct monst *);
void stealarm(void);
int  steal(struct monst *);
void mpickobj(struct monst *, struct obj *);
int  stealamulet(struct monst *);
void relobj(struct monst *, int);

/* hack.termcap.c */
void startup(void);
void start_screen(void);
void end_screen(void);
void start_screen(void);
void curs(int, int);
void cl_end(void);
void clr_screen(void);
void home(void);
void standoutbeg(void);
void standoutend(void);
void backsp(void);
void hackbell(void);
void cl_eos(void);

/* hack.timeout.c */
void hacktimeout(void);

/* hack.topl.c */
int  doredotopl(void);
void remember_topl(void);
void addtopl(char *);
void more(void);
void cmore(char *);
void clrlin(void);
void pline(const char *, ...) __attribute__((__format__ (printf, 1, 2)));
void vpline(const char *, va_list) __attribute__((__format__ (printf, 1, 0)));
void putsym(char);
void putstr(char *);

/* hack.track.c */
void initrack(void);
void settrack(void);
coord *gettrack(int, int);

/* hack.trap.c */
struct trap *maketrap(int, int, int);
void dotrap(struct trap *);
int  mintrap(struct monst *);
void selftouch(char *);
void float_up(void);
int  float_down(void);
void tele(void);
int  dotele(void);
void placebc(int);
void unplacebc(void);
void level_tele(void);
void drown(void);

/* hack.tty.c */
void gettty(void);
void settty(char *);
void setftty(void);
__dead void error(const char *, ...) __attribute__((__format__ (printf, 1, 2)));
void getlin(char *);
void getret(void);
void cgetret(char *);
void xwaitforspace(char *);
char *parse(void);
char readchar(void);
void end_of_input(void);

/* hack.u_init.c */
void u_init(void);
void plnamesuffix(void);

/* hack.unix.c */
int  getyear(void);
char *getdate(void);
int  phase_of_the_moon(void);
int  night(void);
int  midnight(void);
void getlock(void);
#ifdef MAIL
void getmailstatus(void);
void ckmailstatus(void);
void readmail(void);
#endif
void regularize(char *);

/* hack.vault.c */
void setgd(void);
void invault(void);
int  gd_move(void);
void gddead(void);
void replgd(struct monst *, struct monst *);

/* hack.version.c */
int  doversion(void);

/* hack.wield.c */
void setuwep(struct obj *);
int  dowield(void);
void corrode_weapon(void);
int  chwepon(struct obj *, int);

/* hack.wizard.c */
void amulet(void);
int  wiz_hit(struct monst *);
void inrange(struct monst *);
void aggravate(void);

/* hack.worm.c */
#ifndef NOWORM
int  getwn(struct monst *);
void initworm(struct monst *);
void worm_move(struct monst *);
void worm_nomove(struct monst *);
void wormdead(struct monst *);
void wormhit(struct monst *);
void wormsee(unsigned);
void pwseg(struct wseg *);
void cutworm(struct monst *, xchar, xchar, uchar);
#endif

/* hack.worn.c */
void setworn(struct obj *, long);
void setnotworn(struct obj *);

/* hack.zap.c */
void bhitm(struct monst *, struct obj *);
boolean bhito(struct obj *, struct obj *);
int  dozap(void);
char *exclam(int);
void hit(char *, struct monst *, char *);
void miss(char *, struct monst *);
struct monst *bhit(int, int, int, char,
    void (*fhitm)(struct monst *, struct obj *),
    boolean (*fhito)(struct obj *, struct obj *), struct obj *);
struct monst *boomhit(int, int);
void buzz(int, xchar, xchar, int, int);
void fracture_rock(struct obj *);

/* rnd.c */
int  rn1(int, int);
int  rn2(int);
int  rnd(int);
int  d(int, int);
