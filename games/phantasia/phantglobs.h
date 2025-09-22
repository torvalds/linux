/*	$OpenBSD: phantglobs.h,v 1.8 2024/05/21 05:00:47 jsg Exp $	*/
/*	$NetBSD: phantglobs.h,v 1.3 1995/04/24 12:24:39 cgd Exp $	*/

/*
 * phantglobs.h - global declarations for Phantasia
 */

#include <stdio.h>

#include "phantstruct.h"

extern	double	Circle;		/* which circle player is in */
extern	double	Shield;		/* force field thrown up in monster battle */

extern	bool	Beyond;		/* set if player is beyond point of no return */
extern	bool	Marsh;		/* set if player is in dead marshes */
extern	bool	Throne;		/* set if player is on throne */
extern	bool	Changed;	/* set if important player stats have changed */
extern	bool	Wizard;		/* set if player is the 'wizard' of the game */
extern	bool	Timeout;	/* set if short timeout waiting for input */
extern	bool	Windows;	/* set if we are set up for curses stuff */
extern	bool	Luckout;	/* set if we have tried to luck out in fight */
extern	bool	Foestrikes;	/* set if foe gets a chance to hit in battleplayer()*/
extern	bool	Echo;		/* set if echo input to terminal */

extern	int	Users;		/* number of users currently playing */
extern	int	Whichmonster;	/* which monster we are fighting */
extern	int	Lines;		/* line on screen counter for fight routines */

extern	char	Ch_Erase;	/* backspace key */
extern	char	Ch_Kill;	/* linekill key */

extern	long	Fileloc;	/* location in file of player statistics */

extern	const char *Login;	/* pointer to login of current player */
extern	char	*Enemyname;	/* pointer name of monster/player we are battling*/

extern	struct player	Player;	/* stats for player */
extern	struct player	Other;	/* stats for another player */

extern	struct monster	Curmonster;/* stats for current monster */

extern	struct energyvoid Enrgyvoid;/* energy void buffer */

extern	struct charstats Stattable[];/* used for rolling and changing player stats*/

extern	struct charstats *Statptr;/* pointer into Stattable[] */

extern	struct menuitem	Menu[];	/* menu of items for purchase */

extern	FILE	*Playersfp;	/* pointer to open player file */
extern	FILE	*Monstfp;	/* pointer to open monster file */
extern	FILE	*Messagefp;	/* pointer to open message file */
extern	FILE	*Energyvoidfp;	/* pointer to open energy void file */

extern	char	Databuf[SZ_DATABUF];	/* a place to read data into */

/* some canned strings for messages */
extern	char	Illcmd[];
extern	char	Illmove[];
extern	char	Illspell[];
extern	char	Nomana[];
extern	char	Somebetter[];
extern	char	Nobetter[];

/* functions which we need to know about */

char	*descrlocation(struct player *, bool);
char	*descrstatus(struct player *);
char	*descrtype(struct player *, bool);
void	activelist(void);
void	adjuststats(void);
long	allocrecord(void);
long	allocvoid(void);
void	allstatslist(void);
void	altercoordinates(double, double, int);
void	awardtreasure(void);
void	battleplayer(long);
void	callmonster(int);
void	cancelmonster(void);
void	catchalarm(int);
void	changestats(bool);
void	checkbattle(void);
void	checktampered(void);
void	cleanup(int);
void	collecttaxes(double, double);
void	cursedtreasure(void);
void	death(char *);
void	displaystats(void);
double	distance(double, double, double, double);
void	dotampered(void);
double	drandom(void);
void	encounter(int);
void	enterscore(void);
void	error(char *);
double	explevel(double);
long	findname(char *, struct player *);
void	freerecord(struct player *, long);
void	genchar(int);
int	getanswer(char *, bool);
void	getstring(char *, int);
void	hitmonster(double);
double	infloat(void);
void	initialstate(void);
void	initplayer(struct player *);
int	inputoption(void);
void	interrupt(void);
void	leavegame(void);
void	monsthits(void);
void	monstlist(void);
void	more(int);
void	movelevel(void);
void	myturn(void);
void	neatstuff(void);
int	pickmonster(void);
void	playerhits(void);
void	playinit(void);
void	procmain(void);
void	purgeoldplayers(void);
void	readmessage(void);
void	readrecord(struct player *, long);
long	recallplayer(void);
long	rollnewplayer(void);
void	scorelist(void);
void	scramblestats(void);
void	tampered(int, double, double);
void	throneroom(void);
void	throwspell(void);
void	titlelist(void);
void	tradingpost(void);
void	truncstring(char *);
void	userlist(bool);
void	writerecord(struct player *, long);
void	writevoid(struct energyvoid *, long);
