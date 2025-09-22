/*	$OpenBSD: canfield.c,v 1.29 2021/01/21 20:08:17 millert Exp $	*/
/*	$NetBSD: canfield.c,v 1.7 1995/05/13 07:28:35 jtc Exp $	*/

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
 */

/*
 * The canfield program
 *
 * Authors:
 *	Originally written: Steve Levine
 *	Converted to use curses and debugged: Steve Feldman
 *	Card counting: Kirk McKusick and Mikey Olson
 *	User interface cleanups: Eric Allman and Kirk McKusick
 *	Betting by Kirk McKusick
 */

#include <ctype.h>
#include <curses.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define	decksize	52
#define	originrow	0
#define	origincol	0
#define	basecol		1
#define	boxcol		42
#define	tboxrow		2
#define	bboxrow		17
#define	movecol		43
#define	moverow		16
#define	msgcol		43
#define	msgrow		15
#define	titlecol	30
#define	titlerow	0
#define	sidecol		1
#define	ottlrow		6
#define	foundcol	11
#define	foundrow	3
#define	stockcol	2
#define	stockrow	8
#define	fttlcol		10
#define	fttlrow		1
#define	taloncol	2
#define	talonrow	13
#define	tabrow		8
#define	ctoprow		21
#define	cbotrow		23
#define	cinitcol	14
#define	cheightcol	1
#define	cwidthcol	4
#define	handstatrow	21
#define	handstatcol	7
#define	talonstatrow	22
#define	talonstatcol	7
#define	stockstatrow	23
#define	stockstatcol	7
#define	Ace		1
#define	Jack		11
#define	Queen		12
#define	King		13
#define	atabcol		11
#define	btabcol		18
#define	ctabcol		25
#define	dtabcol		32

#define	spades		's'
#define	clubs		'c'
#define	hearts		'h'
#define	diamonds	'd'
#define	black		'b'
#define	red		'r'

#define	stk		1
#define	tal		2
#define	tab		3
#define	INCRHAND(row, col) {\
	row -= cheightcol;\
	if (row < ctoprow) {\
		row = cbotrow;\
		col += cwidthcol;\
	}\
}
#define	DECRHAND(row, col) {\
	row += cheightcol;\
	if (row > cbotrow) {\
		row = ctoprow;\
		col -= cwidthcol;\
	}\
}


struct cardtype {
	char suit;
	char color;
	bool visible;
	bool paid;
	int rank;
	struct cardtype *next;
};

#define	NIL	((struct cardtype *) -1)

struct cardtype *deck[decksize];
struct cardtype cards[decksize];
struct cardtype *bottom[4], *found[4], *tableau[4];
struct cardtype *talon, *hand, *stock, *basecard;
int length[4];
int cardsoff, base, cinhand, taloncnt, stockcnt, timesthru;
char suitmap[4] = {spades, clubs, hearts, diamonds};
char colormap[4] = {black, black, red, red};
char pilemap[4] = {atabcol, btabcol, ctabcol, dtabcol};
char srcpile, destpile;
int mtforigin, tempbase;
int coldcol, cnewcol, coldrow, cnewrow;
bool errmsg, done;
bool mtfdone, Cflag = FALSE;
#define	INSTRUCTIONBOX	1
#define	BETTINGBOX	2
#define	NOBOX		3
int status = INSTRUCTIONBOX;

/*
 * Basic betting costs
 */
#define	costofhand		13
#define	costofinspection	13
#define	costofgame		26
#define	costofrunthroughhand	 5
#define	costofinformation	 1
#define	secondsperdollar	60
#define	maxtimecharge		 3
#define	valuepercardup	 	 5
/*
 * Variables associated with betting 
 */
struct betinfo {
	long	hand;		/* cost of dealing hand */
	long	inspection;	/* cost of inspecting hand */
	long	game;		/* cost of buying game */
	long	runs;		/* cost of running through hands */
	long	information;	/* cost of information */
	long	thinktime;	/* cost of thinking time */
	long	wins;		/* total winnings */
	long	worth;		/* net worth after costs */
};
struct betinfo this, game, total;
bool startedgame = FALSE, infullgame = FALSE;
time_t acctstart;
int dbfd = -1;

void	askquit(int);
__dead void	cleanup(int);
void	cleanupboard(void);
void	clearabovemovebox(void);
void	clearbelowmovebox(void);
void	clearmsg(void);
void	clearstat(void);
void	destinerror(void);
bool	diffcolor(const struct cardtype *, const struct cardtype *);
void	dumberror(void);
bool	finish(void);
void	fndbase(struct cardtype **, int, int);
void	getcmd(int, int, const char *);
void	initall(void);
void	initdeck(struct cardtype *[]);
void	initgame(void);
void	instruct(void);
void	makeboard(void);
void	movebox(void);
void	movecard(void);
void	movetofound(struct cardtype **, int);
void	movetotalon(void);
bool	notempty(const struct cardtype *);
void	printbottombettingbox(void);
void	printbottominstructions(void);
void	printcard(int, int, const struct cardtype *);
void	printrank(int, int, const struct cardtype *, bool);
void	printtopbettingbox(void);
void	printtopinstructions(void);
bool	rankhigher(const struct cardtype *, int);
bool	ranklower(const struct cardtype *, const struct cardtype *);
void	removecard(int, int);
int	samesuit(const struct cardtype *, int);
void	showcards(void);
void	showstat(void);
void	shuffle(struct cardtype *[]);
void	simpletableau(struct cardtype **, int);
void	startgame(void);
void	suspend(void);
bool	tabok(const struct cardtype *, int);
void	tabprint(int, int);
void	tabtotab(int, int);
void	transit(struct cardtype **, struct cardtype **);
void	updatebettinginfo(void);
void	usedstock(void);
void	usedtalon(void);

/*
 * The following procedures print the board onto the screen using the
 * addressible cursor. The end of these procedures will also be
 * separated from the rest of the program.
 *
 * procedure to set the move command box
 */
void
movebox(void)
{
	switch (status) {
	case BETTINGBOX:
		printtopbettingbox();
		break;
	case NOBOX:
		clearabovemovebox();
		break;
	case INSTRUCTIONBOX:
		printtopinstructions();
		break;
	}
	move(moverow, boxcol);
	printw("|                                  |");
	move(msgrow, boxcol);
	printw("|                                  |");
	switch (status) {
	case BETTINGBOX:
		printbottombettingbox();
		break;
	case NOBOX:
		clearbelowmovebox();
		break;
	case INSTRUCTIONBOX:
		printbottominstructions();
		break;
	}
	refresh();
}

/*
 * print directions above move box
 */
void
printtopinstructions(void)
{
	    move(tboxrow, boxcol);
	    printw("*----------------------------------*");
	    move(tboxrow + 1, boxcol);
	    printw("|         MOVES                    |");
	    move(tboxrow + 2, boxcol);
	    printw("|s# = stock to tableau             |");
	    move(tboxrow + 3, boxcol);
	    printw("|sf = stock to foundation          |");
	    move(tboxrow + 4, boxcol);
	    printw("|t# = talon to tableau             |");
	    move(tboxrow + 5, boxcol);
	    printw("|tf = talon to foundation          |");
	    move(tboxrow + 6, boxcol);
	    printw("|## = tableau to tableau           |");
	    move(tboxrow + 7, boxcol);
	    printw("|#f = tableau to foundation        |");
	    move(tboxrow + 8, boxcol);
	    printw("|ht = hand to talon                |");
	    move(tboxrow + 9, boxcol);
	    printw("|c = toggle card counting          |");
	    move(tboxrow + 10, boxcol);
	    printw("|b = present betting information   |");
	    move(tboxrow + 11, boxcol);
	    printw("|q = quit to end the game          |");
	    move(tboxrow + 12, boxcol);
	    printw("|==================================|");
}

/*
 * Print the betting box.
 */
void
printtopbettingbox(void)
{

	    move(tboxrow, boxcol);
	    printw("*----------------------------------*");
	    move(tboxrow + 1, boxcol);
	    printw("|Costs        Hand   Game    Total |");
	    move(tboxrow + 2, boxcol);
	    printw("| Hands                            |");
	    move(tboxrow + 3, boxcol);
	    printw("| Inspections                      |");
	    move(tboxrow + 4, boxcol);
	    printw("| Games                            |");
	    move(tboxrow + 5, boxcol);
	    printw("| Runs                             |");
	    move(tboxrow + 6, boxcol);
	    printw("| Information                      |");
	    move(tboxrow + 7, boxcol);
	    printw("| Think time                       |");
	    move(tboxrow + 8, boxcol);
	    printw("|Total Costs                       |");
	    move(tboxrow + 9, boxcol);
	    printw("|Winnings                          |");
	    move(tboxrow + 10, boxcol);
	    printw("|Net Worth                         |");
	    move(tboxrow + 11, boxcol);
	    printw("|Return                            |");
	    move(tboxrow + 12, boxcol);
	    printw("|==================================|");
}

/*
 * clear info above move box
 */
void
clearabovemovebox(void)
{
	int i;

	for (i = 0; i <= 11; i++) {
		move(tboxrow + i, boxcol);
		printw("                                    ");
	}
	move(tboxrow + 12, boxcol);
	printw("*----------------------------------*");
}

/*
 * print instructions below move box
 */
void
printbottominstructions(void)
{
	    move(bboxrow, boxcol);
	    printw("|Replace # with the number of the  |");
	    move(bboxrow + 1, boxcol);
	    printw("|tableau you want.                 |");
	    move(bboxrow + 2, boxcol);
	    printw("*----------------------------------*");
}

/*
 * print betting information below move box
 */
void
printbottombettingbox(void)
{
	    move(bboxrow, boxcol);
	    printw("|x = toggle information box        |");
	    move(bboxrow + 1, boxcol);
	    printw("|i = list playing instructions     |");
	    move(bboxrow + 2, boxcol);
	    printw("*----------------------------------*");
}

/*
 * clear info below move box
 */
void
clearbelowmovebox(void)
{
	int i;

	move(bboxrow, boxcol);
	printw("*----------------------------------*");
	for (i = 1; i <= 2; i++) {
		move(bboxrow + i, boxcol);
		printw("                                    ");
	}
}

/*
 * procedure to put the board on the screen using addressable cursor
 */
void
makeboard(void)
{
	clear();
	refresh();
	move(titlerow, titlecol);
	printw("=-> CANFIELD <-=");
	move(fttlrow, fttlcol);
	printw("foundation");
	move(foundrow - 1, fttlcol);
	printw("=---=  =---=  =---=  =---=");
	move(foundrow, fttlcol);
	printw("|   |  |   |  |   |  |   |");
	move(foundrow + 1, fttlcol);
	printw("=---=  =---=  =---=  =---=");
	move(ottlrow, sidecol);
	printw("stock     tableau");
	move(stockrow - 1, sidecol);
	printw("=---=");
	move(stockrow, sidecol);
	printw("|   |");
	move(stockrow + 1, sidecol);
	printw("=---=");
	move(talonrow - 2, sidecol);
	printw("talon");
	move(talonrow - 1, sidecol);
	printw("=---=");
	move(talonrow, sidecol);
	printw("|   |");
	move(talonrow + 1, sidecol);
	printw("=---=");
	move(tabrow - 1, atabcol);
	printw("-1-    -2-    -3-    -4-");
	movebox();
}

/*
 * clean up the board for another game
 */
void
cleanupboard(void)
{
	int cnt, row, col;
	struct cardtype *ptr;

	if (Cflag) {
		clearstat();
		for(ptr = stock, row = stockrow;
		    ptr != NIL;
		    ptr = ptr->next, row++) {
			move(row, sidecol);
			printw("     ");
		}
		move(row, sidecol);
		printw("     ");
		move(stockrow + 1, sidecol);
		printw("=---=");
		move(talonrow - 2, sidecol);
		printw("talon");
		move(talonrow - 1, sidecol);
		printw("=---=");
		move(talonrow + 1, sidecol);
		printw("=---=");
	}
	move(stockrow, sidecol);
	printw("|   |");
	move(talonrow, sidecol);
	printw("|   |");
	move(foundrow, fttlcol);
	printw("|   |  |   |  |   |  |   |");
	for (cnt = 0; cnt < 4; cnt++) {
		switch(cnt) {
		case 0:
			col = atabcol;
			break;
		case 1:
			col = btabcol;
			break;
		case 2:
			col = ctabcol;
			break;
		case 3:
			col = dtabcol;
			break;
		}
		for(ptr = tableau[cnt], row = tabrow;
		    ptr != NIL;
		    ptr = ptr->next, row++)
			removecard(col, row);
	}
}

/*
 * procedure to create a deck of cards
 */
void
initdeck(struct cardtype *deck[])
{
	int i;
	int scnt;
	char s;
	int r;

	i = 0;
	for (scnt=0; scnt<4; scnt++) {
		s = suitmap[scnt];
		for (r=Ace; r<=King; r++) {
			deck[i] = &cards[i];
			cards[i].rank = r;
			cards[i].suit = s;
			cards[i].color = colormap[scnt];
			cards[i].next = NIL;
			i++;
		}
	}
}

/*
 * procedure to shuffle the deck
 */
void
shuffle(struct cardtype *deck[])
{
	int i,j;
	struct cardtype *temp;

	for (i=0; i<decksize; i++) {
		deck[i]->visible = FALSE;
		deck[i]->paid = FALSE;
	}
	for (i = decksize - 1; i > 0; i--) {
		j = arc4random_uniform(i + 1);
		if (i != j) {
			temp = deck[i];
			deck[i] = deck[j];
			deck[j] = temp;
		}
	}
}

/*
 * procedure to remove the card from the board
 */
void
removecard(int a, int b)
{
	move(b, a);
	printw("   ");
}

/*
 * procedure to print the cards on the board
 */
void
printrank(int a, int b, const struct cardtype *cp, bool inverse)
{
	move(b, a);
	if (cp->rank != 10)
		addch(' ');
	if (inverse)
		standout();
	switch (cp->rank) {
		case 2: case 3: case 4: case 5: case 6: case 7:
		case 8: case 9: case 10:
			printw("%d", cp->rank);
			break;
		case Ace:
			addch('A');
			break;
		case Jack:
			addch('J');
			break;
		case Queen:
			addch('Q');
			break;
		case King:
			addch('K');
	}
	if (inverse)
		standend();
}

/*
 * procedure to print out a card
 */
void
printcard(int a, int b, const struct cardtype *cp)
{
	if (cp == NIL)
		removecard(a, b);
	else if (cp->visible == FALSE) {
		move(b, a);
		printw(" ? ");
	} else {
		bool inverse = (cp->suit == 'd' || cp->suit == 'h');

		printrank(a, b, cp, inverse);
		if (inverse)
			standout();
		addch(cp->suit);
		if (inverse)
			standend();
	}
}

/*
 * procedure to move the top card from one location to the top 
 * of another location. The pointers always point to the top
 * of the piles.
 */
void
transit(struct cardtype **source, struct cardtype **dest)
{
	struct cardtype *temp;
	
	temp = *source;
	*source = (*source)->next;
	temp->next = *dest;
	*dest = temp;
}

/*
 * Procedure to set the cards on the foundation base when available.
 * Note that it is only called on a foundation pile at the beginning of
 * the game, so the pile will have exactly one card in it.
 */
void
fndbase(struct cardtype **cp, int column, int row)
{
	bool nomore;

	if (*cp != NIL)
		do {
			if ((*cp)->rank == basecard->rank) {
				base++;
				printcard(pilemap[base], foundrow, *cp);
				if (*cp == tableau[0])
					length[0] = length[0] - 1;
				if (*cp == tableau[1])
					length[1] = length[1] - 1;
				if (*cp == tableau[2])
					length[2] = length[2] - 1;
				if (*cp == tableau[3])
					length[3] = length[3] - 1;
				transit(cp, &found[base]);
				if (cp == &talon)
					usedtalon();
				if (cp == &stock)
					usedstock();
				if (*cp != NIL) {
					printcard(column, row, *cp);
					nomore = FALSE;
				} else {
					removecard(column, row);
					nomore = TRUE;
				}
				cardsoff++;
				if (infullgame) {
					this.wins += valuepercardup;
					game.wins += valuepercardup;
					total.wins += valuepercardup;
				}
			} else 
				nomore = TRUE;
	} while (nomore == FALSE);
}

/*
 * procedure to initialize the things necessary for the game
 */
void
initgame(void)
{
	int i;

	for (i=0; i<18; i++) {
		deck[i]->visible = TRUE;
		deck[i]->paid = TRUE;
	}
	stockcnt = 13;
	stock = deck[12];
	for (i=12; i>=1; i--)
		deck[i]->next = deck[i - 1];
	deck[0]->next = NIL;
	found[0] = deck[13];
	deck[13]->next = NIL;
	for (i=1; i<4; i++)
		found[i] = NIL;
	basecard = found[0];
	for (i=14; i<18; i++) {
		tableau[i - 14] = deck[i];
		deck[i]->next = NIL;
	}
	for (i=0; i<4; i++) {
		bottom[i] = tableau[i];
		length[i] = tabrow;
	}
	hand = deck[18];
	for (i=18; i<decksize-1; i++)
		deck[i]->next = deck[i + 1];
	deck[decksize-1]->next = NIL;
	talon = NIL;
	base = 0;
	cinhand = 34;
	taloncnt = 0;
	timesthru = 0;
	cardsoff = 1;
	coldrow = ctoprow;
	coldcol = cinitcol;
	cnewrow = ctoprow;
	cnewcol = cinitcol + cwidthcol;
}

/*
 * procedure to print the beginning cards and to start each game
 */
void
startgame(void)
{
	int j;

	shuffle(deck);
	initgame();
	this.hand = costofhand;
	game.hand += costofhand;
	total.hand += costofhand;
	this.inspection = 0;
	this.game = 0;
	this.runs = 0;
	this.information = 0;
	this.wins = 0;
	this.thinktime = 0;
	infullgame = FALSE;
	startedgame = FALSE;
	printcard(foundcol, foundrow, found[0]);
	printcard(stockcol, stockrow, stock);
	printcard(atabcol, tabrow, tableau[0]);
	printcard(btabcol, tabrow, tableau[1]);
	printcard(ctabcol, tabrow, tableau[2]);
	printcard(dtabcol, tabrow, tableau[3]);
	printcard(taloncol, talonrow, talon);
	move(foundrow - 2, basecol);
	printw("Base");
	move(foundrow - 1, basecol);
	printw("Rank");
	printrank(basecol, foundrow, found[0], 0);
	for (j=0; j<=3; j++)
		fndbase(&tableau[j], pilemap[j], tabrow);
	fndbase(&stock, stockcol, stockrow);
	showstat();	/* show card counting info to cheaters */
	movetotalon();
	updatebettinginfo();
}

/*
 * procedure to clear the message printed from an error
 */
void
clearmsg(void)
{
	int i;

	if (errmsg == TRUE) {
		errmsg = FALSE;
		move(msgrow, msgcol);
		for (i=0; i<25; i++)
			addch(' ');
		refresh();
	}
}

/*
 * procedure to print an error message if the move is not listed
 */
void
dumberror(void)
{
	errmsg = TRUE;
	move(msgrow, msgcol);
	printw("Not a proper move       ");
}

/*
 * procedure to print an error message if the move is not possible
 */
void
destinerror(void)
{
	errmsg = TRUE;
	move(msgrow, msgcol);
	printw("Error: Can't move there");
}

/*
 * function to see if the source has cards in it
 */
bool
notempty(const struct cardtype *cp)
{
	if (cp == NIL) {
		errmsg = TRUE;
		move(msgrow, msgcol);
		printw("Error: no cards to move");
		return (FALSE);
	} else 
		return (TRUE);
}

/*
 * function to see if the rank of one card is less than another
 */
bool
ranklower(const struct cardtype *cp1, const struct cardtype *cp2)
{
	if (cp2->rank == Ace) 
		if (cp1->rank == King)
			return (TRUE);
		else 
			return (FALSE);
	else if (cp1->rank + 1 == cp2->rank)
		return (TRUE);
	else 
		return (FALSE);
}

/*
 * function to check the cardcolor for moving to a tableau
 */
bool
diffcolor(const struct cardtype *cp1, const struct cardtype *cp2)
{
	if (cp1->color == cp2->color)
		return (FALSE);
	else 
		return (TRUE);
}

/*
 * function to see if the card can move to the tableau
 */
bool
tabok(const struct cardtype *cp, int des)
{
	if ((cp == stock) && (tableau[des] == NIL))
		return (TRUE);
	else if (tableau[des] == NIL)
		if (stock == NIL && 
		    cp != bottom[0] && cp != bottom[1] && 
		    cp != bottom[2] && cp != bottom[3])
			return (TRUE);
		else 
			return (FALSE);
	else if (ranklower(cp, tableau[des]) && diffcolor(cp, tableau[des]))
		return (TRUE);
	else 
		return (FALSE);
}

/*
 * procedure to turn the cards onto the talon from the deck
 */
void
movetotalon(void)
{
	int i, fin;

	if (cinhand <= 3 && cinhand > 0) {
		move(msgrow, msgcol);
		printw("Hand is now empty        ");
	}
	if (cinhand >= 3)
		fin = 3;
	else if (cinhand > 0)
		fin = cinhand;
	else if (talon != NIL) {
		timesthru++;
		errmsg = TRUE;
		move(msgrow, msgcol);
		if (timesthru != 4) {
			printw("Talon is now the new hand");
			this.runs += costofrunthroughhand;
			game.runs += costofrunthroughhand;
			total.runs += costofrunthroughhand;
			while (talon != NIL) {
				transit(&talon, &hand);
				cinhand++;
			}
			if (cinhand >= 3)
				fin = 3;
			else
				fin = cinhand;
			taloncnt = 0;
			coldrow = ctoprow;
			coldcol = cinitcol;
			cnewrow = ctoprow;
			cnewcol = cinitcol + cwidthcol;
			clearstat();
			showstat();
		} else {
			fin = 0;
			done = TRUE;
			printw("I believe you have lost");
			refresh();
			sleep(5);
		}
	} else {
		errmsg = TRUE;
		move(msgrow, msgcol);
		printw("Talon and hand are empty");
		fin = 0;
	}
	for (i=0; i<fin; i++) {
		transit(&hand, &talon);
		INCRHAND(cnewrow, cnewcol);
		INCRHAND(coldrow, coldcol);
		removecard(cnewcol, cnewrow);
		if (i == fin - 1)
			talon->visible = TRUE;
		if (Cflag) {
			if (talon->paid == FALSE && talon->visible == TRUE) {
				this.information += costofinformation;
				game.information += costofinformation;
				total.information += costofinformation;
				talon->paid = TRUE;
			}
			printcard(coldcol, coldrow, talon);
		}
	}
	if (fin != 0) {
		printcard(taloncol, talonrow, talon);
		cinhand -= fin;
		taloncnt += fin;
		if (Cflag) {
			move(handstatrow, handstatcol);
			printw("%3d", cinhand);
			move(talonstatrow, talonstatcol);
			printw("%3d", taloncnt);
		}
		fndbase(&talon, taloncol, talonrow);
	}
}


/*
 * procedure to print card counting info on screen
 */
void
showstat(void)
{
	int row, col;
	struct cardtype *ptr;

	if (!Cflag)
		return;
	move(talonstatrow, talonstatcol - 7);
	printw("Talon: %3d", taloncnt);
	move(handstatrow, handstatcol - 7);
	printw("Hand:  %3d", cinhand);
	move(stockstatrow, stockstatcol - 7);
	printw("Stock: %3d", stockcnt);
	for ( row = coldrow, col = coldcol, ptr = talon;
	      ptr != NIL;
	      ptr = ptr->next ) {
		if (ptr->paid == FALSE && ptr->visible == TRUE) {
			ptr->paid = TRUE;
			this.information += costofinformation;
			game.information += costofinformation;
			total.information += costofinformation;
		}
		printcard(col, row, ptr);
		DECRHAND(row, col);
	}
	for ( row = cnewrow, col = cnewcol, ptr = hand;
	      ptr != NIL;
	      ptr = ptr->next ) {
		if (ptr->paid == FALSE && ptr->visible == TRUE) {
			ptr->paid = TRUE;
			this.information += costofinformation;
			game.information += costofinformation;
			total.information += costofinformation;
		}
		INCRHAND(row, col);
		printcard(col, row, ptr);
	}
}

/*
 * procedure to clear card counting info from screen
 */
void
clearstat(void)
{
	int row;

	move(talonstatrow, talonstatcol - 7);
	printw("          ");
	move(handstatrow, handstatcol - 7);
	printw("          ");
	move(stockstatrow, stockstatcol - 7);
	printw("          ");
	for ( row = ctoprow ; row <= cbotrow ; row++ ) {
		move(row, cinitcol);
		printw("%56s", " ");
	}
}

/*
 * procedure to update card counting base
 */
void
usedtalon(void)
{
	removecard(coldcol, coldrow);
	DECRHAND(coldrow, coldcol);
	if (talon != NIL && (talon->visible == FALSE)) {
		talon->visible = TRUE;
		if (Cflag) {
			this.information += costofinformation;
			game.information += costofinformation;
			total.information += costofinformation;
			talon->paid = TRUE;
			printcard(coldcol, coldrow, talon);
		}
	}
	taloncnt--;
	if (Cflag) {
		move(talonstatrow, talonstatcol);
		printw("%3d", taloncnt);
	}
}

/*
 * procedure to update stock card counting base
 */
void
usedstock(void)
{
	stockcnt--;
	if (Cflag) {
		move(stockstatrow, stockstatcol);
		printw("%3d", stockcnt);
	}
}

/*
 * let 'em know how they lost!
 */
void
showcards(void)
{
	struct cardtype *ptr;
	int row;

	if (!Cflag || cardsoff == 52)
		return;
	for (ptr = talon; ptr != NIL; ptr = ptr->next) {
		ptr->visible = TRUE;
		ptr->paid = TRUE;
	}
	for (ptr = hand; ptr != NIL; ptr = ptr->next) {
		ptr->visible = TRUE;
		ptr->paid = TRUE;
	}
	showstat();
	move(stockrow + 1, sidecol);
	printw("     ");
	move(talonrow - 2, sidecol);
	printw("     ");
	move(talonrow - 1, sidecol);
	printw("     ");
	move(talonrow, sidecol);
	printw("     ");
	move(talonrow + 1, sidecol);
	printw("     ");
	for (ptr = stock, row = stockrow; ptr != NIL; ptr = ptr->next, row++) {
		move(row, stockcol - 1);
		printw("|   |");
		printcard(stockcol, row, ptr);
	}
	if (stock == NIL) {
		move(row, stockcol - 1);
		printw("|   |");
		row++;
	}
	move(handstatrow, handstatcol - 7);
	printw("          ");
	move(row, stockcol - 1);
	printw("=---=");
	if ( cardsoff == 52 )
		getcmd(moverow, movecol, "Hit return to exit");
}

/*
 * procedure to update the betting values
 */
void
updatebettinginfo(void)
{
	long thiscosts, gamecosts, totalcosts;
	double thisreturn, gamereturn, totalreturn;
	time_t now;
	long dollars;

	time(&now);
	dollars = (now - acctstart) / secondsperdollar;
	if (dollars > 0) {
		acctstart += dollars * secondsperdollar;
		if (dollars > maxtimecharge)
			dollars = maxtimecharge;
		this.thinktime += dollars;
		game.thinktime += dollars;
		total.thinktime += dollars;
	}
	thiscosts = this.hand + this.inspection + this.game +
		this.runs + this.information + this.thinktime;
	gamecosts = game.hand + game.inspection + game.game +
		game.runs + game.information + game.thinktime;
	totalcosts = total.hand + total.inspection + total.game +
		total.runs + total.information + total.thinktime;
	this.worth = this.wins - thiscosts;
	game.worth = game.wins - gamecosts;
	total.worth = total.wins - totalcosts;
	thisreturn = ((double)this.wins / (double)thiscosts - 1.0) * 100.0;
	gamereturn = ((double)game.wins / (double)gamecosts - 1.0) * 100.0;
	totalreturn = ((double)total.wins / (double)totalcosts - 1.0) * 100.0;
	if (status != BETTINGBOX)
		return;
	move(tboxrow + 2, boxcol + 13);
	printw("%4ld%8ld%9ld", this.hand, game.hand, total.hand);
	move(tboxrow + 3, boxcol + 13);
	printw("%4ld%8ld%9ld", this.inspection, game.inspection,
	    total.inspection);
	move(tboxrow + 4, boxcol + 13);
	printw("%4ld%8ld%9ld", this.game, game.game, total.game);
	move(tboxrow + 5, boxcol + 13);
	printw("%4ld%8ld%9ld", this.runs, game.runs, total.runs);
	move(tboxrow + 6, boxcol + 13);
	printw("%4ld%8ld%9ld", this.information, game.information,
		total.information);
	move(tboxrow + 7, boxcol + 13);
	printw("%4ld%8ld%9ld", this.thinktime, game.thinktime, total.thinktime);
	move(tboxrow + 8, boxcol + 13);
	printw("%4ld%8ld%9ld", thiscosts, gamecosts, totalcosts);
	move(tboxrow + 9, boxcol + 13);
	printw("%4ld%8ld%9ld", this.wins, game.wins, total.wins);
	move(tboxrow + 10, boxcol + 13);
	printw("%4ld%8ld%9ld", this.worth, game.worth, total.worth);
	move(tboxrow + 11, boxcol + 13);
	printw("%4.0f%%%7.1f%%%8.1f%%", thisreturn, gamereturn, totalreturn);
}

/*
 * procedure to move a card from the stock or talon to the tableau
 */
void
simpletableau(struct cardtype **cp, int des)
{
	int origin;

	if (notempty(*cp)) {
		if (tabok(*cp, des)) {
			if (*cp == stock)
				origin = stk;
			else
				origin = tal;
			if (tableau[des] == NIL)
				bottom[des] = *cp;
			transit(cp, &tableau[des]);
			length[des]++;
			printcard(pilemap[des], length[des], tableau[des]);
			timesthru = 0;
			if (origin == stk) {
				usedstock();
				printcard(stockcol, stockrow, stock);
			} else {
				usedtalon();
				printcard(taloncol, talonrow, talon);
			}
		} else 
			destinerror();
	}
}

/*
 * print the tableau
 */
void
tabprint(int sour, int des)
{
	int dlength, slength, i;
	struct cardtype *tempcard;

	for (i=tabrow; i<=length[sour]; i++)
		removecard(pilemap[sour], i);
	dlength = length[des] + 1;
	slength = length[sour];
	if (slength == tabrow)
		printcard(pilemap[des], dlength, tableau[sour]);
	else 
		while (slength != tabrow - 1) {
			tempcard = tableau[sour];
			for (i=1; i<=slength-tabrow; i++)
			    tempcard = tempcard->next;
			printcard(pilemap[des], dlength, tempcard);
			slength--;
			dlength++;
		}
}

/*
 * procedure to move from the tableau to the tableau
 */
void
tabtotab(int sour, int des)
{
	struct cardtype *temp;

	if (notempty(tableau[sour])) {
		if (tabok(bottom[sour], des)) {
			tabprint(sour, des);
			temp = bottom[sour];
			bottom[sour] = NIL;
			if (bottom[des] == NIL)
				bottom[des] = temp;
			temp->next = tableau[des];
			tableau[des] = tableau[sour];
			tableau[sour] = NIL;
			length[des] = length[des] + (length[sour] - (tabrow - 1));
			length[sour] = tabrow - 1;
			timesthru = 0;
		} else 
			destinerror();
	}
}

/*
 * functions to see if the card can go onto the foundation
 */
bool
rankhigher(const struct cardtype *cp, int let)
{
	if (found[let]->rank == King)
		if (cp->rank == Ace)
			return(TRUE);
		else 
			return(FALSE);
	else if (cp->rank - 1 == found[let]->rank)
		return(TRUE);
	else 
		return(FALSE);
}

/*
 * function to determine if two cards are the same suit
 */
int
samesuit(const struct cardtype *cp, int let)
{
	if (cp->suit == found[let]->suit)
		return (TRUE);
	else 
		return (FALSE);
}

/*
 * procedure to move a card to the correct foundation pile
 */
void
movetofound(struct cardtype **cp, int source)
{
	tempbase = 0;
	mtfdone = FALSE;
	if (notempty(*cp)) {
		do {
			if (found[tempbase] != NIL)
				if (rankhigher(*cp, tempbase)
				    && samesuit(*cp, tempbase)) {
					if (*cp == stock)
						mtforigin = stk;
					else if (*cp == talon)
						mtforigin = tal;
					else
						mtforigin = tab;
					transit(cp, &found[tempbase]);
					printcard(pilemap[tempbase],
						foundrow, found[tempbase]);
					timesthru = 0;
					if (mtforigin == stk) {
						usedstock();
						printcard(stockcol, stockrow, stock);
					} else if (mtforigin == tal) {
						usedtalon();
						printcard(taloncol, talonrow, talon);
					} else {
						removecard(pilemap[source], length[source]);
						length[source]--;
					}
					cardsoff++;
					if (infullgame) {
						this.wins += valuepercardup;
						game.wins += valuepercardup;
						total.wins += valuepercardup;
					}
					mtfdone = TRUE;
				} else
					tempbase++;
			else 
				tempbase++;
		} while ((tempbase != 4) && !mtfdone);
		if (!mtfdone)
			destinerror();
	}
}

/*
 * procedure to get a command
 */
void
getcmd(int row, int col, const char *cp)
{
	char cmd[2] = { '\0', '\0'};
	int ch, i;

	i = 0;
	move(row, col);
	printw("%-24s", cp);
	col += 1 + strlen(cp);
	move(row, col);
	refresh();
	do {
		if ((ch = getch()) == ERR)
			cleanup(0);
		if (ch >= KEY_MIN)
			continue;
		if (ch >= 'A' && ch <= 'Z')
			ch += ('a' - 'A');
		if (ch == '\f') {
			wrefresh(curscr);
			refresh();
		} else if (i >= 2 && ch != erasechar() && ch != killchar()) {
			if (ch != '\n' && ch != '\r' && ch != ' ')
				write(1, "\007", 1);
		} else if (ch == erasechar() && i > 0) {
			printw("\b \b");
			refresh();
			cmd[--i] = '\0';
		} else if (ch == killchar() && i > 0) {
			while (i > 0) {
				printw("\b \b");
				cmd[--i] = '\0';
			}
			refresh();
		} else if (ch == '\032') {	/* Control-Z */
			suspend();
			move(row, col + i);
			refresh();
		} else if (isprint(ch)) {
			cmd[i++] = ch;
			addch(ch);
			refresh();
		}
	} while (ch != '\n' && ch != '\r' && ch != ' ');
	srcpile = cmd[0];
	destpile = cmd[1];
}

/*
 * Suspend the game (shell escape if no process control on system)
 */
void
suspend(void)
{

	updatebettinginfo();
	move(21, 0);
	refresh();
	if (dbfd != -1) {
		lseek(dbfd, 0, SEEK_SET);
		write(dbfd, (char *)&total, sizeof(total));
	}
	kill(getpid(), SIGTSTP);
	raw();
	noecho();
}

/*
 * procedure to evaluate and make the specific moves
 */
void
movecard(void)
{
	int source, dest;
	char osrcpile, odestpile;

	done = FALSE;
	errmsg = FALSE;
	do {
		if (talon == NIL && hand != NIL)
			movetotalon();
		if (cardsoff == 52) {
			refresh();
			srcpile = 'q';
		} else if (!startedgame) {
			move(msgrow, msgcol);
			errmsg = TRUE;
			switch (34 - taloncnt - cinhand) {
			default:
				errmsg = FALSE;
				break;
			case 1:
				printw("One card used from talon  ");
				break;
			case 2:
				printw("Two cards used from talon ");
				break;
			case 3:
				printw(">3< cards used from talon ");
				break;
			}
			getcmd(moverow, movecol, "Move:");
		} else
			getcmd(moverow, movecol, "Move:");
		clearmsg();
		if (srcpile >= '1' && srcpile <= '4')
			source = (int) (srcpile - '1');
		if (destpile >= '1' && destpile <= '4')
			dest = (int) (destpile - '1');
		if (!startedgame &&
		    (srcpile == 't' || srcpile == 's' || srcpile == 'h' ||
		     srcpile == '1' || srcpile == '2' || srcpile == '3' ||
		     srcpile == '4')) {
			startedgame = TRUE;
			osrcpile = srcpile;
			odestpile = destpile;
			if (status != BETTINGBOX)
				srcpile = 'y';
			else do {
				getcmd(moverow, movecol, "Inspect game?");
			} while (srcpile != 'y' && srcpile != 'n');
			if (srcpile == 'n') {
				srcpile = 'q';
			} else {
				this.inspection += costofinspection;
				game.inspection += costofinspection;
				total.inspection += costofinspection;
				srcpile = osrcpile;
				destpile = odestpile;
			}
		}
		switch (srcpile) {
			case 't':
				if (destpile == 'f' || destpile == 'F')
					movetofound(&talon, source);
				else if (destpile >= '1' && destpile <= '4')
					simpletableau(&talon, dest);
				else
					dumberror();
				break;
			case 's':
				if (destpile == 'f' || destpile == 'F')
					movetofound(&stock, source);
				else if (destpile >= '1' && destpile <= '4')
					simpletableau(&stock, dest);
				else dumberror();
				break;
			case 'h':
				if (destpile != 't' && destpile != 'T') {
					dumberror();
					break;
				}
				if (infullgame) {
					movetotalon();
					break;
				}
				if (status == BETTINGBOX) {
					do {
						getcmd(moverow, movecol,
							"Buy game?");
					} while (srcpile != 'y' &&
						 srcpile != 'n');
					if (srcpile == 'n') {
						showcards();
						done = TRUE;
						break;
					}
				}
				infullgame = TRUE;
				this.wins += valuepercardup * cardsoff;
				game.wins += valuepercardup * cardsoff;
				total.wins += valuepercardup * cardsoff;
				this.game += costofgame;
				game.game += costofgame;
				total.game += costofgame;
				movetotalon();
				break;
			case 'q':
				showcards();
				done = TRUE;
				break;
			case 'b':
				printtopbettingbox();
				printbottombettingbox();
				status = BETTINGBOX;
				break;
			case 'x':
				clearabovemovebox();
				clearbelowmovebox();
				status = NOBOX;
				break;
			case 'i':
				printtopinstructions();
				printbottominstructions();
				status = INSTRUCTIONBOX;
				break;
			case 'c':
				Cflag = !Cflag;
				if (Cflag)
					showstat();
				else
					clearstat();
				break;
			case '1': case '2': case '3': case '4':
				if (destpile == 'f' || destpile == 'F')
					movetofound(&tableau[source], source);
				else if (destpile >= '1' && destpile <= '4')
					tabtotab(source, dest);
				else dumberror();
				break;
			default:
				dumberror();
		}
		fndbase(&stock, stockcol, stockrow);
		fndbase(&talon, taloncol, talonrow);
		updatebettinginfo();
	} while (!done);
}

const char *const basicinstructions[] = {
	"Here are brief instructions to the game of Canfield:\n\n",
	"     If you have never played solitaire before, it is recom-\n",
	"mended  that  you  consult  a solitaire instruction book. In\n",
	"Canfield, tableau cards may be built on each other  downward\n",
	"in  alternate colors. An entire pile must be moved as a unit\n",
	"in building. Top cards of the piles are available to be able\n",
	"to be played on foundations, but never into empty spaces.\n\n",
	"     Spaces must be filled from the stock. The top  card  of\n",
	"the  stock  also is available to be played on foundations or\n",
	"built on tableau piles. After the stock  is  exhausted,  ta-\n",
	"bleau spaces may be filled from the talon and the player may\n",
	"keep them open until he wishes to use them.\n\n",
	"     Cards are dealt from the hand to the  talon  by  threes\n",
	"and  this  repeats until there are no more cards in the hand\n",
	"or the player quits. To have cards dealt onto the talon  the\n",
	"player  types  'ht'  for his move. Foundation base cards are\n",
	"also automatically moved to the foundation when they  become\n",
	"available.\n\n",
	"push any key when you are finished: ",
	0 };

const char *const bettinginstructions[] = {
	"     The rules for betting are  somewhat  less  strict  than\n",
	"those  used in the official version of the game. The initial\n",
	"deal costs $13. You may quit at this point  or  inspect  the\n",
	"game.  Inspection  costs  $13 and allows you to make as many\n",
	"moves as is possible without moving any cards from your hand\n",
	"to  the  talon.  (the initial deal places three cards on the\n",
	"talon; if all these cards are  used,  three  more  are  made\n",
	"available)  Finally, if the game seems interesting, you must\n",
	"pay the final installment of $26.  At  this  point  you  are\n",
	"credited  at the rate of $5 for each card on the foundation;\n",
	"as the game progresses you are credited  with  $5  for  each\n",
	"card  that is moved to the foundation.  Each run through the\n",
	"hand after the first costs $5.  The  card  counting  feature\n",
	"costs  $1  for  each unknown card that is identified. If the\n",
	"information is toggled on, you are only  charged  for  cards\n",
	"that  became  visible  since it was last turned on. Thus the\n",
	"maximum cost of information is $34.  Playing time is charged\n",
	"at a rate of $1 per minute.\n\n",
	"push any key when you are finished: ",
	0 };

/*
 * procedure to printout instructions
 */
void
instruct(void)
{
	const char *const *cp;

	move(originrow, origincol);
	printw("This is the game of solitaire called Canfield.  Do\n");
	printw("you want instructions for the game?");
	do {
		getcmd(originrow + 3, origincol, "y or n?");
	} while (srcpile != 'y' && srcpile != 'n');
	if (srcpile == 'n')
		return;
	clear();
	for (cp = basicinstructions; *cp != 0; cp++)
		printw("%s", *cp);
	refresh();
	getch();
	clear();
	move(originrow, origincol);
	printw("Do you want instructions for betting?");
	do {
		getcmd(originrow + 2, origincol, "y or n?");
	} while (srcpile != 'y' && srcpile != 'n');
	if (srcpile == 'n')
		return;
	clear();
	for (cp = bettinginstructions; *cp != 0; cp++)
		printw("%s", *cp);
	refresh();
	getch();
}

/*
 * procedure to initialize the game
 */
void
initall(void)
{
	int i, ret;
	char scorepath[PATH_MAX];
	const char *home;

	time(&acctstart);
	initdeck(deck);

	home = getenv("HOME");
	if (home == NULL || *home == '\0')
		err(1, "getenv");

	ret = snprintf(scorepath, sizeof(scorepath), "%s/%s", home,
	    ".cfscores");
	if (ret < 0 || ret >= PATH_MAX)
		errc(1, ENAMETOOLONG, "%s/%s", home, ".cfscores");

	dbfd = open(scorepath, O_RDWR | O_CREAT, 0644);
	if (dbfd < 0)
		return;
	i = read(dbfd, (char *)&total, sizeof(total));
	if (i < 0) {
		close(dbfd);
		dbfd = -1;
		return;
	}
}

/*
 * procedure to end the game
 */
bool
finish(void)
{
	int row, col;

	if (cardsoff == 52) {
		getcmd(moverow, movecol, "Hit return to exit");
		clear();
		refresh();
		move(originrow, origincol);
		printw("CONGRATULATIONS!\n");
		printw("You won the game. That is a feat to be proud of.\n");
		row = originrow + 5;
		col = origincol;
	} else {
		move(msgrow, msgcol);
		printw("You got %d card", cardsoff);
		if (cardsoff > 1)
			printw("s");
		printw(" off    ");
		move(msgrow, msgcol);
		row = moverow;
		col = movecol;
	}
	do {
		getcmd(row, col, "Play again (y or n)?");
	} while (srcpile != 'y' && srcpile != 'n');
	errmsg = TRUE;
	clearmsg();
	if (srcpile == 'y')
		return (FALSE);
	else
		return (TRUE);
}

/*
 * procedure to clean up and exit
 */
void
cleanup(int dummy)
{

	total.thinktime += 1;
	status = NOBOX;
	updatebettinginfo();
	if (dbfd != -1) {
		lseek(dbfd, 0, SEEK_SET);
		write(dbfd, (char *)&total, sizeof(total));
		close(dbfd);
	}
	clear();
	move(22,0);
	refresh();
	endwin();
	exit(0);
}

/*
 * Field an interrupt.
 */
void
askquit(int dummy)
{
	move(msgrow, msgcol);
	printw("Really wish to quit?    ");
	do {
		getcmd(moverow, movecol, "y or n?");
	} while (srcpile != 'y' && srcpile != 'n');
	clearmsg();
	if (srcpile == 'y')
		cleanup(0);
	signal(SIGINT, askquit);
}

/*
 * Can you tell that this used to be a Pascal program?
 */
int
main(int argc, char *argv[])
{
	signal(SIGINT, askquit);
	signal(SIGHUP, cleanup);
	signal(SIGTERM, cleanup);
	initscr();
	raw();
	noecho();
	initall();

	if (pledge("stdio tty", NULL) == -1)
		err(1, "pledge");

	instruct();
	makeboard();
	for (;;) {
		startgame();
		movecard();
		if (finish())
			break;
		if (cardsoff == 52)
			makeboard();
		else
			cleanupboard();
	}
	cleanup(0);
}
