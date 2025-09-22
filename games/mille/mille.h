/*	$OpenBSD: mille.h,v 1.13 2016/01/08 18:05:58 mestre Exp $	*/
/*	$NetBSD: mille.h,v 1.5 1995/03/24 05:01:51 cgd Exp $	*/

/*
 * Copyright (c) 1982, 1993
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
 *	@(#)mille.h	8.1 (Berkeley) 5/31/93
 */

#include <sys/uio.h>

#include <curses.h>

/*
 * @(#)mille.h	1.1 (Berkeley) 4/1/82
 */

/*
 * Miscellaneous constants
 */

#define	CARD		short

#define	HAND_SZ		7	/* number of cards in a hand	*/
#define	DECK_SZ		101	/* number of cards in decks	*/
#define	NUM_SAFE	4	/* number of safety cards	*/
#define	NUM_MILES	5	/* number of milestones types	*/
#define	NUM_CARDS	20	/* number of types of cards	*/
#define	BOARD_Y		17	/* size of board screen		*/
#define	BOARD_X		40
#define	MILES_Y		7	/* size of mileage screen	*/
#define	MILES_X		80
#define	SCORE_Y		17	/* size of score screen		*/
#define	SCORE_X		40
#define	MOVE_Y		10	/* Where to print move prompt	*/
#define	MOVE_X		20
#define	ERR_Y		15	/* Where to print errors	*/
#define	ERR_X		5
#define	EXT_Y		4	/* Where to put Extension	*/
#define	EXT_X		9

#define	PLAYER		0
#define	COMP		1

#define	W_SMALL		0	/* Small (initial) window	*/
#define	W_FULL		1	/* Full (final) window		*/

/*
 * Move types
 */

#define	M_DISCARD	0
#define	M_DRAW		1
#define	M_PLAY		2
#define	M_ORDER		3

/*
 * Scores
 */

#define	SC_SAFETY	100
#define	SC_ALL_SAFE	300
#define	SC_COUP		300
#define	SC_TRIP		400
#define	SC_SAFE		300
#define	SC_DELAY	300
#define	SC_EXTENSION	200
#define	SC_SHUT_OUT	500

/*
 * safety descriptions
 */

#define	S_UNKNOWN	0	/* location of safety unknown	*/
#define	S_IN_HAND	1	/* safety in player's hand	*/
#define	S_PLAYED	2	/* safety has been played	*/
#define	S_GAS_SAFE	0	/* Gas safety card index	*/
#define	S_SPARE_SAFE	1	/* Tire safety card index	*/
#define	S_DRIVE_SAFE	2	/* Driveing safety card index	*/
#define	S_RIGHT_WAY	3	/* Right-of-Way card index	*/
#define	S_CONV		15	/* conversion from C_ to S_	*/

/*
 * card numbers
 */

#define	C_INIT		-1
#define	C_25		0
#define	C_50		1
#define	C_75		2
#define	C_100		3
#define	C_200		4
#define	C_EMPTY		5
#define	C_FLAT		6	
#define	C_CRASH		7
#define	C_STOP		8
#define	C_LIMIT		9
#define	C_GAS		10
#define	C_SPARE		11
#define	C_REPAIRS	12
#define	C_GO		13
#define	C_END_LIMIT	14
#define	C_GAS_SAFE	15
#define	C_SPARE_SAFE	16
#define	C_DRIVE_SAFE	17
#define	C_RIGHT_WAY	18

/*
 * prompt types
 */

#define	MOVEPROMPT		0
#define	REALLYPROMPT		1
#define	ANOTHERHANDPROMPT	2
#define	ANOTHERGAMEPROMPT	3
#define	SAVEGAMEPROMPT		4
#define	SAMEFILEPROMPT		5
#define	FILEPROMPT		6
#define	EXTENSIONPROMPT		7
#define	OVERWRITEFILEPROMPT	8

typedef struct {
	bool	coups[NUM_SAFE];
	bool	can_go;
	bool	new_battle;
	bool	new_speed;
	short	safety[NUM_SAFE];
	short	sh_safety[NUM_SAFE];
	short	nummiles[NUM_MILES];
	short	sh_nummiles[NUM_MILES];
	CARD	hand[HAND_SZ];
	CARD	sh_hand[HAND_SZ];
	CARD	battle;
	CARD	sh_battle;
	CARD	speed;
	CARD	sh_speed;
	int	mileage;
	int	sh_mileage;
	int	hand_tot;
	int	sh_hand_tot;
	int	safescore;
	int	sh_safescore;
	int	coupscore;
	int	total;
	int	sh_total;
	int	games;
	int	sh_games;
	int	was_finished;
} PLAY;

/*
 * macros
 */

#undef 	CTRL
#define	CTRL(c)		((c) & 0x1f)
#define	other(x)	(1 - x)
#define	nextplay()	(Play = other(Play))
#define	nextwin(x)	(1 - x)
#define	opposite(x)	(Opposite[x])
#define	is_safety(x)	(x >= C_GAS_SAFE)

/*
 * externals
 */

extern bool	Debug, Finished, Next, On_exit, Order, Saved;

extern char	Initstr[100], *C_fmt;
extern const char	*const *C_name, *Fromfile;

extern int	Card_no, End, Handstart, Movetype, Numgos,
		Numneed[], Numseen[NUM_CARDS], Play, Window;
extern const int	Numcards[], Value[];

extern CARD	Deck[DECK_SZ], Discard, Sh_discard, *Topcard;
extern const CARD	Opposite[NUM_CARDS];

extern FILE	*outf;

extern PLAY	Player[2];

extern WINDOW	*Board, *Miles, *Score;

/*
 * functions
 */

void	account(CARD);
void	calcmove(void);
int	canplay(const PLAY *, const PLAY *, CARD);
int	check_ext(bool);
void	check_go(void);
void	check_more(void);
__dead void	die(int);
void	domove(void);
bool	error(char *, ...);
void	finalscore(PLAY *);
CARD	getcard(void);
void	getmove(void);
int	getyn(int);
int	haspicked(const PLAY *);
void	init(void);
int	is_repair(CARD);
void	newboard(void);
void	newscore(void);
int	onecard(const PLAY *);
int	playcard(PLAY *);
void	prboard(void);
void	prompt(int);
void	prscore(bool);
int	readch(void);
bool	rest_f(const char *);
int	roll(int, int);
void	rub(int);
int	safety(CARD);
bool	save(void);
void	show_card(int, int, CARD, CARD *);
void	show_score(int, int, int, int *);
void	shuffle(void);
void	sort(CARD *);
bool	varpush(int, ssize_t(int, const struct iovec *, int));
#ifdef EXTRAP
void	extrapolate(PLAY *);
void	undoex(void);
#endif
