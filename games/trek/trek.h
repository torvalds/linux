/*	$OpenBSD: trek.h,v 1.15 2021/01/27 01:57:37 deraadt Exp $	*/
/*	$NetBSD: trek.h,v 1.3 1995/04/22 10:59:36 cgd Exp $	*/

/*
 * Copyright (c) 1980, 1993
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
 *	@(#)trek.h	8.1 (Berkeley) 5/31/93
 */

/*********************  GALAXY  **************************/

/* galactic parameters */
#define	NSECTS		10	/* dimensions of quadrant in sectors */
#define	NQUADS		8	/* dimension of galaxy in quadrants */
#define	NINHAB		32	/* number of quadrants which are inhabited */

struct quad		/* definition for each quadrant */
{
	unsigned char	bases;	/* number of bases in this quadrant */
	char	klings;		/* number of Klingons in this quadrant */
	signed   char	holes;	/* number of black holes in this quadrant */
	int	scanned;	/* star chart entry (see below) */
	short	stars;		/* number of stars in this quadrant */
	char	qsystemname;	/* starsystem name (see below) */
};

#define	Q_DISTRESSED	0200
#define	Q_SYSTEM	077

/*  systemname conventions:
 *	1 -> NINHAB	index into Systemname table for live system.
 *	+ Q_DISTRESSED	distressed starsystem -- systemname & Q_SYSTEM
 *			is the index into the Event table which will
 *			have the system name
 *	0		dead or nonexistent starsystem
 *
 *  starchart ("scanned") conventions:
 *	0 -> 999	taken as is
 *	-1		not yet scanned ("...")
 *	1000		supernova ("///")
 *	1001		starbase + ??? (".1.")
*/

/* ascii names of systems */
extern const char	*const Systemname[NINHAB];

/* quadrant definition */
extern struct quad	Quad[NQUADS][NQUADS];

/* defines for sector map  (below) */
#define	EMPTY		'.'
#define	STAR		'*'
#define	BASE		'#'
#define	ENTERPRISE	'E'
#define	QUEENE		'Q'
#define	KLINGON		'K'
#define	INHABIT		'@'
#define	HOLE		' '

/* current sector map */
extern char	Sect[NSECTS][NSECTS];


/************************ DEVICES ******************************/

#define	NDEV		16	/* max number of devices */

/* device tokens */
#define	WARP		0	/* warp engines */
#define	SRSCAN		1	/* short range scanners */
#define	LRSCAN		2	/* long range scanners */
#define	PHASER		3	/* phaser control */
#define	TORPED		4	/* photon torpedo control */
#define	IMPULSE		5	/* impulse engines */
#define	SHIELD		6	/* shield control */
#define	COMPUTER	7	/* on board computer */
#define	SSRADIO		8	/* subspace radio */
#define	LIFESUP		9	/* life support systems */
#define	SINS		10	/* Space Inertial Navigation System */
#define	CLOAK		11	/* cloaking device */
#define	XPORTER		12	/* transporter */
#define	SHUTTLE		13	/* shuttlecraft */

/* device names */
struct device
{
	const char	*name;		/* device name */
	const char	*person;	/* the person who fixes it */
};

extern const struct device	Device[NDEV];

/***************************  EVENTS  ****************************/

#define	NEVENTS		12	/* number of different event types */

#define	E_LRTB		1	/* long range tractor beam */
#define	E_KATSB		2	/* Klingon attacks starbase */
#define	E_KDESB		3	/* Klingon destroys starbase */
#define	E_ISSUE		4	/* distress call is issued */
#define	E_ENSLV		5	/* Klingons enslave a quadrant */
#define	E_REPRO		6	/* a Klingon is reproduced */
#define	E_FIXDV		7	/* fix a device */
#define	E_ATTACK	8	/* Klingon attack during rest period */
#define	E_SNAP		9	/* take a snapshot for time warp */
#define	E_SNOVA		10	/* supernova occurs */

#define	E_GHOST		0100	/* ghost of a distress call if ssradio out */
#define	E_HIDDEN	0200	/* event that is unreportable because ssradio out */
#define	E_EVENT		077	/* mask to get event code */

struct event
{
	unsigned char	x, y;		/* coordinates */
	double		date;		/* trap stardate */
	char		evcode;		/* event type */
	unsigned char	systemname;	/* starsystem name */
};
/* systemname conventions:
 *	1 -> NINHAB	index into Systemname table for reported distress calls
 *
 * evcode conventions:
 *	1 -> NEVENTS-1	event type
 *	+ E_HIDDEN	unreported (SSradio out)
 *	+ E_GHOST	actually already expired
 *	0		unallocated
 */

#define	MAXEVENTS	25	/* max number of concurrently pending events */

extern struct event	Event[MAXEVENTS];	/* dynamic event list; one entry per pending event */

/*****************************  KLINGONS  *******************************/

struct kling
{
	unsigned char	x, y;	/* coordinates */
	int	power;		/* power left */
	double	dist;		/* distance to Enterprise */
	double	avgdist;	/* average over this move */
	char	srndreq;	/* set if surrender has been requested */
};

#define	MAXKLQUAD	9	/* maximum klingons per quadrant */

/********************** MISCELLANEOUS ***************************/

/* condition codes */
#define	GREEN		0
#define	DOCKED		1
#define	YELLOW		2
#define	RED		3

/* starbase coordinates */
#define	MAXBASES	9	/* maximum number of starbases in galaxy */

/*  distress calls  */
#define	MAXDISTR	5	/* maximum concurrent distress calls */

/* phaser banks */
#define	NBANKS		6	/* number of phaser banks */

struct xy
{
	unsigned char	x, y;		/* coordinates */
};


/*
 *	note that much of the stuff in the following structs CAN NOT
 *	be moved around!!!!
 */


/* information regarding the state of the starship */
extern struct Ship
{
	double	warp;		/* warp factor */
	double	warp2;		/* warp factor squared */
	double	warp3;		/* warp factor cubed */
	char	shldup;		/* shield up flag */
	char	cloaked;	/* set if cloaking device on */
	int	energy;		/* starship's energy */
	int	shield;		/* energy in shields */
	double	reserves;	/* life support reserves */
	int	crew;		/* ship's complement */
	int	brigfree;	/* space left in brig */
	char	torped;		/* torpedoes */
	char	cloakgood;	/* set if we have moved */
	int	quadx;		/* quadrant x coord */
	int	quady;		/* quadrant y coord */
	int	sectx;		/* sector x coord */
	int	secty;		/* sector y coord */
	unsigned char	cond;	/* condition code */
	char	sinsbad;	/* Space Inertial Navigation System condition */
	const char	*shipname;	/* name of current starship */
	char	ship;		/* current starship */
	int	distressed;	/* number of distress calls */
}	Ship;

/* sinsbad is set if SINS is working but not calibrated */

/* game related information, mostly scoring */
extern struct Game
{
	int	killk;		/* number of klingons killed */
	int	deaths;		/* number of deaths onboard Enterprise */
	char	negenbar;	/* number of hits on negative energy barrier */
	char	killb;		/* number of starbases killed */
	int	kills;		/* number of stars killed */
	char	skill;		/* skill rating of player */
	char	length;		/* length of game */
	char	killed;		/* set if you were killed */
	char	killinhab;	/* number of inhabited starsystems killed */
	char	tourn;		/* set if a tournament game */
	char	passwd[15];	/* game password */
	char	snap;		/* set if snapshot taken */
	char	helps;		/* number of help calls */
	int	captives;	/* total number of captives taken */
}	Game;

/* per move information */
extern struct Move
{
	char	free;		/* set if a move is free */
	char	endgame;	/* end of game flag */
	char	shldchg;	/* set if shields changed this move */
	char	newquad;	/* set if just entered this quadrant */
	char	resting;	/* set if this move is a rest */
	double	time;		/* time used this move */
}	Move;

/* parametric information */
extern struct Param
{
	unsigned char	bases;	/* number of starbases */
	char	klings;		/* number of klingons */
	double	date;		/* stardate */
	double	time;		/* time left */
	double	resource;	/* Federation resources */
	int	energy;		/* starship's energy */
	int	shield;		/* energy in shields */
	double	reserves;	/* life support reserves */
	int	crew;		/* size of ship's complement */
	int	brigfree;	/* max possible number of captives */
	char	torped;		/* photon torpedos */
	double	damfac[NDEV];	/* damage factor */
	double	dockfac;	/* docked repair time factor */
	double	regenfac;	/* regeneration factor */
	int	stopengy;	/* energy to do emergency stop */
	int	shupengy;	/* energy to put up shields */
	int	klingpwr;	/* Klingon initial power */
	int	warptime;	/* time chewer multiplier */
	double	phasfac;	/* Klingon phaser power eater factor */
	char	moveprob[6];	/* probability that a Klingon moves */
	double	movefac[6];	/* Klingon move distance multiplier */
	double	eventdly[NEVENTS];	/* event time multipliers */
	double	navigcrud[2];	/* navigation crudup factor */
	int	cloakenergy;	/* cloaking device energy per stardate */
	double	damprob[NDEV];	/* damage probability */
	double	hitfac;		/* Klingon attack factor */
	int	klingcrew;	/* number of Klingons in a crew */
	double	srndrprob;	/* surrender probability */
	int	energylow;	/* low energy mark (cond YELLOW) */
}	Param;

/* Sum of damage probabilities must add to 1000 */

/* other information kept in a snapshot */
extern struct Now
{
	unsigned char	bases;	/* number of starbases */
	char	klings;		/* number of klingons */
	double	date;		/* stardate */
	double	time;		/* time left */
	double	resource;	/* Federation resources */
	char	distressed;	/* number of currently distressed quadrants */
	struct event	*eventptr[NEVENTS];	/* pointer to event structs */
	struct xy	base[MAXBASES];		/* locations of starbases */
}	Now;

/* Other stuff, not dumped in a snapshot */
extern struct Etc
{
	struct kling	klingon[MAXKLQUAD];	/* sorted Klingon list */
	short		nkling;			/* number of Klingons in this sector */
						/* < 0 means automatic override mode */
	struct xy	starbase;	/* starbase in current quadrant */
	char		snapshot[sizeof Quad + sizeof Event + sizeof Now];	/* snapshot for time warp */
	char		statreport;		/* set to get a status report on a srscan */
}	Etc;

/*
 *	eventptr is a pointer to the event[] entry of the last
 *	scheduled event of each type.  Zero if no such event scheduled.
 */

/* Klingon move indices */
#define	KM_OB		0	/* Old quadrant, Before attack */
#define	KM_OA		1	/* Old quadrant, After attack */
#define	KM_EB		2	/* Enter quadrant, Before attack */
#define	KM_EA		3	/* Enter quadrant, After attack */
#define	KM_LB		4	/* Leave quadrant, Before attack */
#define	KM_LA		5	/* Leave quadrant, After attack */

/* you lose codes */
#define	L_NOTIME	1	/* ran out of time */
#define	L_NOENGY	2	/* ran out of energy */
#define	L_DSTRYD	3	/* destroyed by a Klingon */
#define	L_NEGENB	4	/* ran into the negative energy barrier */
#define	L_SUICID	5	/* destroyed in a nova */
#define	L_SNOVA		6	/* destroyed in a supernova */
#define	L_NOLIFE	7	/* life support died (so did you) */
#define	L_NOHELP	8	/* you could not be rematerialized */
#define	L_TOOFAST	9	/* pretty stupid going at warp 10 */
#define	L_STAR		10	/* ran into a star */
#define	L_DSTRCT	11	/* self destructed */
#define	L_CAPTURED	12	/* captured by Klingons */
#define	L_NOCREW	13	/* you ran out of crew */

/******************  COMPILE OPTIONS  ***********************/

/* Trace info */
/* #define	xTRACE		1 */
#ifdef xTRACE
int	Trace;
#endif

/* abandon.c */
void abandon(int);

/* attack.c */
void attack(int);

/* autover.c */
void autover(void);

/* capture.c */
void capture(int);
struct kling *selectklingon(void);

/* check_out.c */
int check_out(int);

/* checkcond.c */
void checkcond(void);

/* compkl.c */
void compkldist(int);

/* computer.c */
void computer(int);

/* damage.c */
void damage(int, double);

/* damaged.c */
int damaged(int);

/* dcrept.c */
void dcrept(int);

/* destruct.c */
void destruct(int);

/* dock.c */
void dock(int);
void undock(int);

/* dumpme.c */
void dumpme(int);

/* dumpssradio.c */
int dumpssradio(void);

/* events.c */
int events(int);

/* externs.c */

/* getcodi.c */
int getcodi(int *, double *);

/* help.c */
void help(int);

/* impulse.c */
void impulse(int);

/* initquad.c */
void initquad(int);
void sector(int *, int *);

/* kill.c */
void killk(int, int );
void killb(int, int );
void kills(int, int , int);
void killd(int, int , int);

/* klmove.c */
void klmove(int);

/* lose.c */
void lose(int);

/* lrscan.c */
void lrscan(int);

/* move.c */
double move(int, int, double, double);

/* nova.c */
void nova(int, int );

/* out.c */
void out(int);

/* phaser.c */
void phaser(int);

/* play.c */
void myreset(int);
void play(void);

/* ram.c */
void ram(int, int );

/* ranf.c */
int ranf(int);
double franf(void);

/* rest.c */
void rest(int);

/* schedule.c */
struct event *schedule(int, double, int, int , int);
void reschedule(struct event *, double);
void unschedule(struct event *);
struct event *xsched(int, int, int, int , int );
void xresched(struct event *, int, int);

/* score.c */
long score(void);

/* setup.c */
void setup(void);

/* setwarp.c */
void setwarp(int);

/* shield.c */
void shield(int);

/* snova.c */
void snova(int, int );

/* srscan.c */
void srscan(int);

/* systemname.c */
const char *systemname(const struct quad *);

/* torped.c */
void torped(int);

/* visual.c */
void visual(int);

/* warp.c */
void dowarp(int);
void warp(int, int, double);

/* win.c */
void win(void);
